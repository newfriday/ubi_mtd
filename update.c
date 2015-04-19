#include <linux/math64.h>
#include "ubi.h"
#include "fastscan.h"

struct ubi_wl_entry *pebs[UBI_FASTSCAN_PEB_COUNT];
/**
 	计算快扫描元数据长度，分配内存空间
 */
size_t fastscan_calc_fs_size(struct ubi_device *ubi)
{
	size_t size;
	size = sizeof(struct fastscan_metadata_hdr) + \
		   sizeof(struct fastscan_metadata_wl) * ubi->peb_count + \
		   sizeof(struct fastscan_metadata_vol_info) * UBI_MAX_VOLUMES + \
		   sizeof(struct fastscan_metadata_eba) * UBI_MAX_VOLUMES + \
		   sizeof(__be32) * ubi->peb_count;
	return roundup(size, ubi->leb_size);
}
/**
 *	分配vid header
 *	内部删除卷
 *	卷ID = 布局卷 + 1
 */
static struct ubi_vid_hdr *new_fs_hdr(struct ubi_device *ubi, int vol_id)
{
	struct ubi_vid_hdr *new;

	new = ubi_zalloc_vid_hdr(ubi, GFP_KERNEL);
	if(!new)
		goto out;

	new->vol_type = UBI_FASTSCAN_VOLUME_TYPE;
	new->vol_id = cpu_to_be32(vol_id);
	new->compat = UBI_FASTSCAN_VOLUME_COMPAT;
out:
	return new;
}

int fastscan_alloc_pebs(struct ubi_device *ubi, struct ubi_wl_entry **pebs)
{
	int ret;

	ret = fastscan_find_pebs(&ubi->free, pebs); 
	if(ret != 0)
	{
		ubi_msg("failed to alloc pebs");
		return -1;
	}
	return 0;
}

int fastscan_write_metadata(struct ubi_device *ubi, struct ubi_wl_entry **pebs)
{
	/***********变量分配***********/
	int i, j, ret = 0;
	void *fs_raw;
	size_t fs_pos = 0;
	struct ubi_vid_hdr *fs_vhdr;
	struct rb_node *node;
	struct ubi_wl_entry *wl_e;
	struct ubi_work *ubi_wrk;
	struct ubi_volume *vol;

	int free_peb_count;
	int used_peb_count; 
	int scrub_peb_count; 
	int erase_peb_count;
	int bad_peb_count; 
	int vol_count; 
	int used_blocks;

	struct fastscan_metadata_hdr *fs_meta_hdr;
	struct fastscan_metadata_wl *fs_meta_wl;
	struct fastscan_metadata_vol_info *fs_meta_vol_info;
	struct fastscan_metadata_eba *fs_meta_eba;

	/***********写缓冲区初始化***********/
	ubi_msg("init fs_buf");
	ubi_assert(ubi->fs_buf);

	fs_raw = ubi->fs_buf;
	memset(ubi->fs_buf, 0, ubi->fs_size);
	ubi_msg("fs_size %d", ubi->fs_size);

	/***********分配元数据内部卷头部***********/
	ubi_msg("alloc fastscan volume header");
	fs_vhdr = new_fs_hdr(ubi, UBI_FASTSCAN_VOLUME_ID);
	if(!fs_vhdr)
	{
		ubi_msg("failed to alloc fastscan volume header");
		ret = -ENOMEM;	
		goto out;
	}		
	
	spin_lock(&ubi->volumes_lock);
	spin_lock(&ubi->wl_lock);

	/***********初始化第一段数据：fastscan_metadata_hdr对应内容***********/
	ubi_msg("alloc fastscan_metadata_hdr");
	fs_meta_hdr = (struct fastscan_metadata_hdr *)fs_raw;
	fs_pos += sizeof(*fs_meta_hdr);
	ubi_assert(fs_pos <= ubi->fs_size);
	
	fs_meta_hdr->magic = cpu_to_be32(UBI_FASTSCAN_HDR_MAGIC);
	free_peb_count = 0;
	used_peb_count = 0;
	scrub_peb_count = 0;
	erase_peb_count = 0;
	bad_peb_count = 0;
	vol_count = 0;
	used_blocks = ubi->fs_size / ubi->leb_size;

	/***********收集free红黑树的擦除信息填充到fs_raw,同时累加空闲的擦出块数***********/
	ubi_msg("collect pnum and ec data of free PEB from free red black tree");
	for(node = rb_first(&ubi->free); node; node = rb_next(node))
	{
		wl_e = rb_entry(node, struct ubi_wl_entry, u.rb);
		ubi_assert(wl_e);
		fs_meta_wl = (struct fastscan_metadata_wl *)(fs_raw + fs_pos);	
		
		fs_meta_wl->pnum = cpu_to_be32(wl_e->pnum);
		fs_meta_wl->ec = cpu_to_be32(wl_e->ec);

		free_peb_count++;
		fs_pos += sizeof(*fs_meta_wl);
		ubi_assert(fs_pos <= ubi->fs_size);
	}
	fs_meta_hdr->free_peb_count = cpu_to_be32(free_peb_count);

	ubi_msg("collect pnum and ec data of used PEB from used red black tree");
	for(node = rb_first(&ubi->used); node; node = rb_next(node))
	{
		wl_e = rb_entry(node, struct ubi_wl_entry, u.rb);
		ubi_assert(wl_e);
		fs_meta_wl = (struct fastscan_metadata_wl *)(fs_raw + fs_pos);	
		
		fs_meta_wl->pnum = cpu_to_be32(wl_e->pnum);
		fs_meta_wl->ec = cpu_to_be32(wl_e->ec);

		used_peb_count++;
		fs_pos += sizeof(*fs_meta_wl);
		ubi_assert(fs_pos <= ubi->fs_size);
	}
	fs_meta_hdr->used_peb_count = cpu_to_be32(used_peb_count);

	ubi_msg("collect pnum and ec data of scrub PEB from scrub red black tree");
	for(node = rb_first(&ubi->scrub); node; node = rb_next(node))
	{
		wl_e = rb_entry(node, struct ubi_wl_entry, u.rb);
		ubi_assert(wl_e);
		fs_meta_wl = (struct fastscan_metadata_wl *)(fs_raw + fs_pos);	
		
		fs_meta_wl->pnum = cpu_to_be32(wl_e->pnum);
		fs_meta_wl->ec = cpu_to_be32(wl_e->ec);

		scrub_peb_count++;
		fs_pos += sizeof(*fs_meta_wl);
		ubi_assert(fs_pos <= ubi->fs_size);
	}
	fs_meta_hdr->scrub_peb_count = cpu_to_be32(scrub_peb_count);

	/***********从WL子系统工作队列中收集erase状态的擦除块信息填入fs_raw***********/
	ubi_msg("collect pnum and ec data of erase PEB from WL subsystem work queue");
	list_for_each_entry(ubi_wrk, &ubi->works, list)
	{
		if(ubi_is_erase_work(ubi_wrk))
		{
			wl_e = ubi_wrk->e;
			ubi_assert(wl_e);

			fs_meta_wl = (struct fastscan_metadata_wl *)(fs_raw + fs_pos);	
		
			fs_meta_wl->pnum = cpu_to_be32(wl_e->pnum);
			fs_meta_wl->ec = cpu_to_be32(wl_e->ec);

			erase_peb_count++;
			fs_pos += sizeof(*fs_meta_wl);
			ubi_assert(fs_pos <= ubi->fs_size);
		}
	}
	fs_meta_hdr->erase_peb_count = cpu_to_be32(erase_peb_count);

	/***********collect volume-related metadata to fullfill the fs_raw***********/
	ubi_msg("collect volume-related data from ubi->volumes");
	for(i = 0; i < UBI_MAX_VOLUMES + UBI_INT_VOL_COUNT; i++)
	{
		vol = ubi->volumes[i];
		
		if(!vol)
			continue;

		vol_count++;
		fs_meta_vol_info = (struct fastscan_metadata_vol_info *)(fs_raw + fs_pos);
		fs_pos += sizeof(*fs_meta_vol_info);

		fs_meta_vol_info->magic = cpu_to_be32(UBI_FASTSCAN_VOL_MAGIC);		
		fs_meta_vol_info->vol_id = cpu_to_be32(vol->vol_id);		
		fs_meta_vol_info->vol_type = vol->vol_type;		
		fs_meta_vol_info->used_ebs = cpu_to_be32(vol->used_ebs);		
		fs_meta_vol_info->data_pad = cpu_to_be32(vol->data_pad);		
		fs_meta_vol_info->last_eb_bytes = cpu_to_be32(vol->last_eb_bytes);		
		
		ubi_assert(vol->vol_type == UBI_DYNAMIC_VOLUME || vol->vol_type == UBI_STATIC_VOLUME);

		fs_meta_eba = (struct fastscan_metadata_eba *)(fs_raw + fs_pos);
		fs_pos += sizeof(*fs_meta_eba);

		fs_meta_eba->magic = cpu_to_be32(UBI_FASTSCAN_EBA_MAGIC);
		for(j = 0; j < vol->reserved_pebs; j++)
			fs_meta_eba->pnum[j] = cpu_to_be32(vol->eba_tbl[j]);
		fs_meta_eba->peb_num = cpu_to_be32(j);
	}
	fs_meta_hdr->vol_count = cpu_to_be32(vol_count);
	fs_meta_hdr->bad_peb_count = cpu_to_be32(ubi->bad_peb_count);
	fs_meta_hdr->used_blocks = cpu_to_be32(used_blocks);

	ubi_msg("collect data done!");

	spin_unlock(&ubi->wl_lock);
	spin_unlock(&ubi->volumes_lock);

	/***********写卷头部到used_blocks个PEB中去***********/
	ubi_msg("writing fastscan volume header to Flash");
	for(i = 0; i < used_blocks; i++)
	{
		fs_vhdr->sqnum = cpu_to_be32(next_sqnum(ubi));
		fs_vhdr->lnum = i;
		ubi_msg("writing fastscan volume header to PEB %d, sqnum %llu", 
						pebs[i]->pnum, fs_vhdr->sqnum);
		ret = ubi_io_write_vid_hdr(ubi, pebs[i]->pnum, fs_vhdr);
		if(ret != 0)
		{
			ubi_msg("failed to write fs_vhdr to PEB %i",pebs[i]->pnum); 
			goto out_kfree;
		}
	}
	
	/***********写元数据到used_blocks个PEB中去***********/
	ubi_msg("writing fastscan data to Flash %d blocks", used_blocks);
	for(i = 0; i < used_blocks; i++)
	{
		ubi_msg("writing fs_raw data to PEB %d", pebs[i]->pnum);
		ret = ubi_io_write(ubi, fs_raw + (i * ubi->leb_size), 
						pebs[i]->pnum, ubi->leb_start, ubi->leb_size);	
		if(ret != 0)
		{
			ubi_msg("failed to write data to PEB %i",pebs[i]->pnum); 
			goto out_kfree;
		}
	}
	/*
	for(i = used_blocks; i < UBI_FASTSCAN_PEB_COUNT; i++)
	{
		ret = ubi_wl_put_peb(ubi, pebs[i]->pnum, 0);
	}
	*/	

	ubi_assert(pebs);
	ubi->pebs = pebs;
	ubi->used_blocks = used_blocks;
	ubi_msg("write fastscan done!");

out_kfree:
	ubi_free_vid_hdr(ubi, fs_vhdr);
out:
	return ret;
}
/**
 *	更新元数据
 *	返回值
 *	成功：			0
 *	失败：	无空闲PEB	-1
 *		写入失败	-2
 */
int fastscan_update_metadata(struct ubi_device *ubi)
{
	int i, ret;

	for(i = 0; i < UBI_FASTSCAN_PEB_COUNT; i++)
		pebs[i] = NULL;

	ret = fastscan_alloc_pebs(ubi, pebs);
	if(ret != 0)
	{
		return	-1; 
	}
	ubi_msg("find available pebs");	
	for(i = 0; i < UBI_FASTSCAN_PEB_COUNT; i++)
		ubi_msg("pebs[%d] = %d", i, pebs[i]->pnum);

	ret = fastscan_write_metadata(ubi, pebs);
	if(ret != 0)
	{
		ubi_msg("failed to update metadata ret %d", ret);	
		return -2;
	}
	return ret;
}

