#include "ubi.h"
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








