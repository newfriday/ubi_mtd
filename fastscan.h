/* Fast scan stuff */
#define UBI_FASTSCAN_END		128
#define UBI_FASTSCAN_VOLUME_ID		(UBI_LAYOUT_VOLUME_ID+1)
#define UBI_FASTSCAN_VOLUME_TYPE	(UBI_VID_DYNAMIC)
#define UBI_FASTSCAN_VOLUME_ALIGN	1
#define UBI_FASTSCAN_VOLUME_EBA		4
#define UBI_FASTSCAN_VOLUME_NAME   "fastscan volume"
#define UBI_FASTSCAN_VOLUME_COMPAT 	UBI_COMPAT_DELETE   
/* PEB count reserved for fastscan metadata */
#define UBI_FASTSCAN_PEB_COUNT		4
/* ASCII: HDR! */
#define UBI_FASTSCAN_HDR_MAGIC		0x48445221
/* ASCII: VOL! */
#define UBI_FASTSCAN_VOL_MAGIC		0x564F4C21
/* ASCII: EBA! */
#define UBI_FASTSCAN_EBA_MAGIC		0x45424121

/**
 *
 */
struct fastscan_metadata_hdr{
	__be32		magic;
	__be32		free_peb_count;
	__be32		used_peb_count;
	__be32		scrub_peb_count;
	__be32		erase_peb_count;
	__be32		bad_peb_count;
	__be32		vol_count;
	__u8		padding[4];
} __packed;

/**
 *
 */
struct fastscan_metadata_wl{
	__be32		pnum;
	__be32		ec;
} __packed;

/**
 *
 */
struct fastscan_metadata_vol_info{
	__be32		magic;
	__be32		vol_id;
	__u8		vol_type;
	__u8		padding[3];
	__be32		data_pad;
	__be32		used_ebs;
	__be32		last_eb_bytes;
} __packed;

/**
 *
 */
struct fastscan_metadata_eba{
	__be32		magic;
	__be32		peb_num;
	__be32		pnum[0];
} __packed;


