#include <soc/samsung/fvmap.h>

enum acpm_dvfs_id {
	MIF = ACPM_VCLK_TYPE,
	INT,
	CPUCL0,
	CPUCL1,
	G3D,
	AUD,
	CAM,
	DISP,
	CP,
};

struct vclk acpm_vclk_list[] = {
	CMUCAL_ACPM_VCLK(MIF, NULL, NULL, NULL, NULL, MARGIN_MIF),
	CMUCAL_ACPM_VCLK(INT, NULL, NULL, NULL, NULL, MARGIN_INT),
	CMUCAL_ACPM_VCLK(CPUCL0, NULL, NULL, NULL, NULL, MARGIN_CPUCL0),
	CMUCAL_ACPM_VCLK(CPUCL1, NULL, NULL, NULL, NULL, MARGIN_CPUCL1),
	CMUCAL_ACPM_VCLK(G3D, NULL, NULL, NULL, NULL, MARGIN_G3D),
	CMUCAL_ACPM_VCLK(AUD, NULL, NULL, NULL, NULL, MARGIN_AUD),
	CMUCAL_ACPM_VCLK(CAM, NULL, NULL, NULL, NULL, MARGIN_CAM),
	CMUCAL_ACPM_VCLK(DISP, NULL, NULL, NULL, NULL, MARGIN_DISP),
	CMUCAL_ACPM_VCLK(CP, NULL, NULL, NULL, NULL, MARGIN_CP),
};

unsigned int acpm_vclk_size = ARRAY_SIZE(acpm_vclk_list);
