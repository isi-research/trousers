
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "trousers/tss.h"
#include "trousers_types.h"
#include "tcs_tsp.h"
#include "tcsps.h"
#include "tcs_utils.h"
#include "tcs_int_literals.h"
#include "capabilities.h"
#include "tcslog.h"
#include "req_mgr.h"
#include "tcsd_wrap.h"
#include "tcsd.h"
#include "tddl.h"


TSS_RESULT
TCSP_RequestLocality_Internal(TCS_CONTEXT_HANDLE hContext,	/* in */
			 UINT32 localityRequested)	/* in */
{
	UINT64 offset = 0;
	UINT32 paramSize;
	TSS_RESULT result;
	BYTE txBlob[TSS_TPM_TXBLOB_SIZE];

	if (localityRequested > 4) {
		LogDebugFn("invalid locality! (%u)", localityRequested);
		return TCSERR(TSS_E_BAD_PARAMETER);
	}

	if ((result = ctx_verify_context(hContext)))
		return result;

    Tddli_RequestLocality(localityRequested);

	LogResult("TCSP_RequestLocality_Internal", result);
	return result;
}

