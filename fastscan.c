#include "ubi.h"
#include "fastscan.h"

struct ubi_vid_hdr *fs_vid_hdr;
int peb_count = 0;
/*
 	读取PEB卷头部信息
	成功：0
	失败：负数
 */
int fastscan_read_vid_hdr(struct ubi_device *ubi, int pnum, 
				struct ubi_vid_hdr *vid_hdr)
{
	int err;

	/* check if the PEB is bad */
	err = ubi_io_is_bad(ubi, pnum);
	if(err < 0)
	{
		ubi_msg("failed to check if PEB is bad");
		return err;
	}
	/* if the PEB is bad, return 0, skip it */
	else if(err)
	{
		return 0;
	}
	/* read the volume id header */
	err = ubi_io_read_vid_hdr(ubi, pnum, vid_hdr, 0);
	if(err < 0)
	{
		ubi_msg("failed to read vid header");
		return err;
	}
	return 0;
}
/*
 	读取PEB，找到其vid_header中的卷ID
	成功：0
	失败：负数
 */
int fastscan_scan_peb(struct ubi_device *ubi, int pnum, int *vid)
{
	int err;

	fs_vid_hdr = ubi_zalloc_vid_hdr(ubi, GFP_KERNEL);

	ubi_msg("scan peb %d", pnum);	
	err = fastscan_read_vid_hdr(ubi, pnum, fs_vid_hdr);
	if(err != 0)
	{
		ubi_msg("failed to read vid hdr");	
		return err;
	}
	
	*vid = fs_vid_hdr->vol_id;
	return 0;
}
/*
 	读取PEB内容
	成功：0
	失败：负数
 */
int fastscan_scan(struct ubi_device *ubi, int *pebs) 
{
	int i, err;

	memset(ubi->fs_buf, 0, ubi->fs_size);	

	for(i = 0; i < peb_count; i++)
	{
		err = ubi_io_read(ubi, ubi->fs_buf + (i * ubi->leb_size), pebs[i], 
				ubi->leb_start, ubi->leb_size);
		if(err < 0)
		{
			ubi_msg("failed to read metadata on PEB %d", pebs[i]);	
			return err;
		}
	}
	return 0;
}

static int add_peb_to_list(struct ubi_scan_info *si, struct list_head *list, 
					int pnum, int ec, int scrub)
{
	struct ubi_scan_leb *scan_eb;
	
	scan_eb = (struct ubi_scan_leb *)kzalloc(sizeof(struct ubi_scan_leb), GFP_KERNEL);
	if(scan_eb == NULL)	
	{
		ubi_msg("failed to alloc memory for ubi_scan_leb");
		return -1;
	}
	scan_eb->pnum = pnum;
	scan_eb->ec = ec;
	scan_eb->scrub = scrub;	

	si->ec_sum += scan_eb->ec;
	si->ec_count++;

	if(si->max_ec < scan_eb->ec)
		si->max_ec = scan_eb->ec;

	if(si->min_ec > scan_eb->ec)
		si->min_ec = scan_eb->ec;

	list_add_tail(&scan_eb->u.list, list);

	return 0;
}

static struct ubi_scan_volume *add_vol_to_rbtree(struct ubi_scan_info *si, int vol_id,
					int used_ebs, int data_pad, u8 vol_type, int last_eb_bytes)
{
	struct ubi_scan_volume *scan_vol;
	struct rb_node **p = &si->volumes.rb_node;
	struct rb_node *parent = NULL;

	scan_vol = (struct ubi_scan_volume *)kzalloc(sizeof(struct ubi_scan_volume), GFP_KERNEL);
	if(scan_vol == NULL)
	{
		ubi_msg("failed to alloc memory for ubi_scan_volume");	
		return NULL;
	}

	while(*p)
	{
		parent = *p;
		scan_vol = rb_entry(parent, struct ubi_scan_volume, rb);

		if(vol_id < scan_vol->vol_id)
			p = &(*p)->rb_left;
		else if(vol_id > scan_vol->vol_id)
			p = &(*p)->rb_right;
	}

	scan_vol->vol_id = vol_id;
	scan_vol->highest_lnum = 0;
	scan_vol->leb_count = 0;
	scan_vol->vol_type = (int)vol_type;
	scan_vol->used_ebs = used_ebs;
	scan_vol->last_data_size = last_eb_bytes;
	scan_vol->data_pad = data_pad;
	scan_vol->compat = 0;
	scan_vol->root= RB_ROOT;

	rb_link_node(&scan_vol->rb, parent, p);
	rb_insert_color(&scan_vol->rb, &si->volumes);

	ubi_msg("get a ubi volume");	
	return scan_vol;
}
/*
 	根据ubi中的信息，构造扫描信息
	成功：0
	失败：负数
 */
static int fastscan_rebuild_scan_info(struct ubi_device *ubi, struct ubi_scan_info *si)
{
	/***********变量分配***********/
	int i, j, ret = 0;
	void *fs_raw;
	size_t fs_pos = 0;
	size_t fs_size = 0;
	struct ubi_scan_leb *scan_eb;
	struct ubi_scan_volume *scan_vol;

	struct fastscan_metadata_hdr *fs_meta_hdr;
	struct fastscan_metadata_wl *fs_meta_wl;
	struct fastscan_metadata_vol_info *fs_meta_vol_info;
	struct fastscan_metadata_eba *fs_meta_eba;

	struct list_head free, used;

	/***********变量初始化***********/
	INIT_LIST_HEAD(&free);
	INIT_LIST_HEAD(&used);

	INIT_LIST_HEAD(&((si)->corr));
	INIT_LIST_HEAD(&((si)->free));
	INIT_LIST_HEAD(&((si)->erase));
	INIT_LIST_HEAD(&((si)->alien));
	
	si->volumes = RB_ROOT;
	si->min_ec = UBI_MAX_ERASECOUNTER;
	si->max_ec = -1;

	/***********读取统计信息***********/
	fs_raw = ubi->fs_buf;
	fs_size = ubi->fs_size;
	fs_meta_hdr = (struct fastscan_metadata_hdr *)fs_raw;
	fs_pos += sizeof(*fs_meta_hdr);
	if(fs_pos >= fs_size)
		goto bad_metadata;

	if(be32_to_cpu(fs_meta_hdr->magic) != UBI_FASTSCAN_HDR_MAGIC ||
				be32_to_cpu(fs_meta_hdr->used_blocks) != peb_count)
	{
		ubi_msg("corrupted metadata header");	
		goto bad_metadata;
	}	

	/***********读取WL子系统需要的空闲擦除块信息***********/
	for(i = 0; i < be32_to_cpu(fs_meta_hdr->free_peb_count); i++)
	{
		fs_meta_wl = (struct fastscan_metadata_wl *)(fs_raw + fs_pos);
		fs_pos += sizeof(*fs_meta_wl);

		if(fs_pos >= fs_size)
			goto bad_metadata;
		
		add_peb_to_list(si, &si->free, be32_to_cpu(fs_meta_wl->pnum),
						be32_to_cpu(fs_meta_wl->ec), 0);
	}

	/***********读取WL子系统需要的使用擦除块信息***********/
	for(i = 0; i < be32_to_cpu(fs_meta_hdr->used_peb_count); i++)
	{
		fs_meta_wl = (struct fastscan_metadata_wl *)(fs_raw + fs_pos);
		fs_pos += sizeof(*fs_meta_wl);

		if(fs_pos >= fs_size)
			goto bad_metadata;
		
		add_peb_to_list(si, &used, be32_to_cpu(fs_meta_wl->pnum),
						be32_to_cpu(fs_meta_wl->ec), 0);
	}

	/***********读取WL子系统需要的需要清洗的擦除块信息***********/
	for(i = 0; i < be32_to_cpu(fs_meta_hdr->scrub_peb_count); i++)
	{
		fs_meta_wl = (struct fastscan_metadata_wl *)(fs_raw + fs_pos);
		fs_pos += sizeof(*fs_meta_wl);

		if(fs_pos >= fs_size)
			goto bad_metadata;
		
		add_peb_to_list(si, &used, be32_to_cpu(fs_meta_wl->pnum),
						be32_to_cpu(fs_meta_wl->ec), 1);
	}

	/***********读取WL子系统需要的需要擦除的擦除块信息***********/
	for(i = 0; i < be32_to_cpu(fs_meta_hdr->erase_peb_count); i++)
	{
		fs_meta_wl = (struct fastscan_metadata_wl *)(fs_raw + fs_pos);
		fs_pos += sizeof(*fs_meta_wl);

		if(fs_pos >= fs_size)
			goto bad_metadata;
		
		add_peb_to_list(si, &si->erase, be32_to_cpu(fs_meta_wl->pnum),
						be32_to_cpu(fs_meta_wl->ec), 1);
	}

	si->mean_ec = div_u64(si->ec_sum, si->ec_count);
	si->bad_peb_count = be32_to_cpu(fs_meta_hdr->bad_peb_count);

	for(i = 0; i < be32_to_cpu(fs_meta_hdr->vol_count); i++)
	{
		fs_meta_vol_info = (struct fastscan_metadata_vol_info *)(fs_raw + fs_pos);
		fs_pos += sizeof(*fs_meta_vol_info);
	
		if(fs_pos >= fs_size)
			goto bad_metadata;

		if(be32_to_cpu(fs_meta_vol_info->magic) != UBI_FASTSCAN_VOL_MAGIC)
		{
			ubi_msg("corrupted metadata volume info");	
			goto bad_metadata;
		}	
		scan_vol = add_vol_to_rbtree(si, be32_to_cpu(fs_meta_vol_info->vol_id),
								be32_to_cpu(fs_meta_vol_info->used_ebs),	
								be32_to_cpu(fs_meta_vol_info->data_pad),	
								fs_meta_vol_info->vol_type,		
								be32_to_cpu(fs_meta_vol_info->last_eb_bytes));	
		if(scan_vol == NULL)
		{
			ubi_msg("failed to add volume info");	
			goto bad_metadata;
		}

	}
bad_metadata:
	ret = -1;
out:
	return ret;
}
/*
 	快扫描：读取PEB上元数据，构造扫描信息
	成功：0
	失败：负数
 */
int fastscan(struct ubi_device *ubi, struct ubi_scan_info **si)
{
	int i, ret, vid; 
	int pebs[UBI_FASTSCAN_PEB_COUNT];

	*si = (struct ubi_scan_info *)kzalloc(sizeof(struct ubi_scan_info), GFP_KERNEL);
	if((*si) == NULL)
	{
		ubi_msg("failed to alloc memory for ubi_scan_info");	
		return -1;
	}

	for(i = 0; i < UBI_FASTSCAN_END; i++)
	{
		fastscan_scan_peb(ubi, i, &vid);
		if(vid == UBI_FASTSCAN_VOLUME_ID)
		{
			pebs[peb_count++] = i;	
		}
	}

	if(peb_count >= 1)
	{
		ret = fastscan_scan(ubi, pebs);	
		if(ret != 0)
		{
			ubi_msg("failed to scan metadata");	
			return ret;
		}
		ret = fastscan_rebuild_scan_info(ubi, (*si));
		if(ret != 0)
		{
			ubi_msg("failed to rebuild scan info");	
			return ret;
		}
	}
}




