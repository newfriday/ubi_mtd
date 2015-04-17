#include <linux/math64.h>
#include "ubi.h"
#include "fastscan.h"
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

struct ubi_wl_entry* fastscan_alloc_pebs(struct ubi_device *ubi)
{

}

int fastscan_write_metadata(struct ubi_device *ubi, struct ubi_wl_entry *pebs)
{
	/***********变量分配***********/
	int ret, i, j;
	void *fs_raw;
	size_t fs_pos = 0;
	struct ubi_vid_hdr *fs_vhdr;
	struct rb_node *node;
	struct ubi_wl_entry *wl_e;
	struct ubi_work *ubi_wrk;
	struct ubi_volume *vol;

	struct fastscan_metadata_hdr *fs_meta_hdr;
	int free_peb_count, used_peb_count, scrub_peb_count, erase_peb_count, vol_count; 
	struct fastscan_metadata_wl *fs_meta_wl;
	struct fastscan_metadata_vol_info *fs_meta_vol_info;
	struct fastscan_metadata_eba *fs_meta_eba;

	/***********写缓冲区初始化***********/
	fs_raw = ubi->fs_buf;
	memset(ubi->fs_buf, 0, ubi->fs_size);

	/***********分配元数据内部卷头部***********/
	fs_vhdr = new_fs_hdr(ubi, UBI_FASTSCAN_VOLUME_ID);
	if(!fs_vhdr)
	{
		ret = -ENOMEM;	
		goto out;
	}		
	
	spin_lock(&ubi->volumes_lock);
	spin_lock(&ubi->wl_lock);

	/***********初始化第一段数据：fastscan_metadata_hdr对应内容***********/
	fs_meta_hdr = (struct fastscan_metadata_hdr *)fs_raw;
	fs_pos += sizeof(*fs_meta_hdr);
	ubi_assert(fs_pos <= ubi->fs_size);
	
	fs_meta_hdr->magic = cpu_to_be32(UBI_FASTSCAN_HDR_MAGIC);
	free_peb_count = 0;
	used_peb_count = 0;
	scrub_peb_count = 0;
	erase_peb_count = 0;
	vol_count = 0;

	/***********收集free红黑树的擦除信息填充到fs_raw,同时累加空闲的擦出块数***********/
	for(node = rb_first(&ubi->free); node; node = rb_next(node))
	{
		wl_e = rb_entry(node, struct ubi_wl_entry, u.rb);
		fs_meta_wl = (struct fastscan_metadata_wl *)(fs_raw + fs_pos);	
		
		fs_meta_wl->pnum = cpu_to_be32(wl_e->pnum);
		fs_meta_wl->ec = cpu_to_be32(wl_e->ec);

		free_peb_count++;
		fs_pos += sizeof(*fs_meta_wl);
		ubi_assert(fs_pos <= ubi->fs_size);
	}
	fs_meta_hdr->free_peb_count = cpu_to_be32(free_peb_count);

	for(node = rb_first(&ubi->used); node; node = rb_next(node))
	{
		wl_e = rb_entry(node, struct ubi_wl_entry, u.rb);
		fs_meta_wl = (struct fastscan_metadata_wl *)(fs_raw + fs_pos);	
		
		fs_meta_wl->pnum = cpu_to_be32(wl_e->pnum);
		fs_meta_wl->ec = cpu_to_be32(wl_e->ec);

		used_peb_count++;
		fs_pos += sizeof(*fs_meta_wl);
		ubi_assert(fs_pos <= ubi->fs_size);
	}
	fs_meta_hdr->used_peb_count = cpu_to_be32(used_peb_count);

	for(node = rb_first(&ubi->scrub); node; node = rb_next(node))
	{
		wl_e = rb_entry(node, struct ubi_wl_entry, u.rb);
		fs_meta_wl = (struct fastscan_metadata_wl *)(fs_raw + fs_pos);	
		
		fs_meta_wl->pnum = cpu_to_be32(wl_e->pnum);
		fs_meta_wl->ec = cpu_to_be32(wl_e->ec);

		scrub_peb_count++;
		fs_pos += sizeof(*fs_meta_wl);
		ubi_assert(fs_pos <= ubi->fs_size);
	}
	fs_meta_hdr->scrub_peb_count = cpu_to_be32(scrub_peb_count);

	/***********从WL子系统工作队列中收集erase状态的擦除块信息填入fs_raw***********/
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
		fs_meta_vol_info->vol_type = vol_type;		
		fs_meta_vol_info->used_pebs = cpu_to_be32(vol->used_pebs);		
		fs_meta_vol_info->data_pad = cpu_to_be32(vol->data_pad);		
		fs_meta_vol_info->last_eb_bytes = cpu_to_be32(vol->last_eb_bytes);		
		
		ubi_assert(vol->vol_type == UBI_DYNAMIC_VOLUME || vol->type == UBI_STATIC_VOLUME);

		fs_meta_eba = (struct fastscan_metadata_eba *)(fs_raw + fs_pos);
		fs_pos += sizeof(*fs_meta_eba);

		fs_meta_eba->magic = cpu_to_be32(UBI_FASTSCAN_EBA_MAGIC);
		for(j = 0; j < vol->reserved_pebs; j++)
			fs_meta_eba->pnum[j] = cpu_to_be32(vol->eba_tbl[j]);
		fs_meta_eba->peb_num = cpu_to_be32(j);
	}
	fs_meta_hdr->vol_count = cpu_to_be32(vol_count);

out_kfree:
	ubi_free_vid_hdr(ubi, fs_vhdr);
out:
	return ret;
}

int fastscan_update_metadata(struct ubi_device *ubi)
{

}

