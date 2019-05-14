

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "trousers/tss.h"
#include "trousers/trousers.h"
#include "trousers_types.h"
#include "spi_utils.h"
#include "capabilities.h"
#include "tsplog.h"
#include "obj.h"


TSS_RESULT
Tspi_TPM_RequestLocality(TSS_HTPM hTPM,		/* in */
		   UINT32 ulLocality)
{
	TSS_HCONTEXT tspContext;
	TSS_RESULT result;

	if ( ulLocality > 4 )
		return TSPERR(TSS_E_BAD_PARAMETER);

	if ((result = obj_tpm_get_tsp_context(hTPM, &tspContext)))
		return result;

    LogDebug("Tspi_TPM_RequestLocality: locality = %d\n", ulLocality);

	if ((result = TCS_API(tspContext)->RequestLocality(tspContext, ulLocality)))
    {
		return result;
    }

	return TSS_SUCCESS;
}

