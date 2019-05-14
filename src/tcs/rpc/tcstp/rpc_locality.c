

#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <netdb.h>

#include "trousers/tss.h"
#include "trousers_types.h"
#include "tcs_tsp.h"
#include "tcs_utils.h"
#include "tcs_int_literals.h"
#include "capabilities.h"
#include "tcslog.h"
#include "tcsd_wrap.h"
#include "tcsd.h"
#include "tcs_utils.h"
#include "rpc_tcstp_tcs.h"

TSS_RESULT
tcs_wrap_RequestLocality(struct tcsd_thread_data *data)
{
	TCS_CONTEXT_HANDLE hContext;
	UINT32 locDataSizeIn;
	BYTE *locDataIn;
	TSS_RESULT result;

	if (getData(TCSD_PACKET_TYPE_UINT32, 0, &hContext, 0, &data->comm))
    {
        LogDebug("tcs_wrap_RequestLocality: failed to get data for index 0\n"); 
		return TCSERR(TSS_E_INTERNAL_ERROR);
    }

	if ((result = ctx_verify_context(hContext)))
		goto done;

	LogDebugFn("thread %ld context %x", THREAD_ID, hContext);

    LogDebug("rpc_locality.c: tcs_wrap_RequestLocality. num_parms=%d\n", 
        data->comm.hdr.num_parms);

	if (getData(TCSD_PACKET_TYPE_UINT32, 1, &locDataSizeIn, 0, &data->comm))
    {
        LogDebug("tcs_wrap_RequestLocality: failed to get data for index 1\n"); 
		return TCSERR(TSS_E_INTERNAL_ERROR);
    }

	locDataIn = (BYTE *)malloc(locDataSizeIn);
	if (locDataIn == NULL) {
		LogError("malloc of %u bytes failed.", locDataSizeIn);
		return TCSERR(TSS_E_OUTOFMEMORY);
	}

	MUTEX_LOCK(tcsp_lock);

	result = TCSP_RequestLocality_Internal(hContext, locDataSizeIn, locDataIn);

	MUTEX_UNLOCK(tcsp_lock);
	free(locDataIn);
done:
	initData(&data->comm, 0);
	data->comm.hdr.u.result = result;

	return result;
}

