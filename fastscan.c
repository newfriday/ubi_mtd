#include "ubi.h"
/*
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
/*
 	读取PEB卷头部信息
	成功：0
	失败：负数
 */
int fastscan_read_vid_hdr(struct ubi_device *ubi, int pnum, 
				struct ubi_vid_hdr *vid_hdr)
{

}
/*
 	读取PEB，找到其vid_header中的卷ID
	成功：0
	失败：负数
 */
int fastscan_scan_peb(struct ubi_device *ubi, int pnum, int *vid)
{

}
/*
 	读取PEB内容
	成功：0
	失败：负数
 */
int fastscan_read(struct ubi_device *ubi, void *buf, int pnum, 
				int offset, int len)
{

}
/*
 	根据ubi中的信息，构造扫描信息
	成功：0
	失败：负数
 */
int fastscan_create_scan_info(struct ubi_device *ubi, struct ubi_scan_info *si)
{

}
/*
 	快扫描：读取PEB上元数据，构造扫描信息
	成功：0
	失败：负数
 */
int fastscan(struct ubi_device *ubi, struct ubi_scan_info *si)
{

}








