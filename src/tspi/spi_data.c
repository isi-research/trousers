
/*
 * Licensed Materials - Property of IBM
 *
 * trousers - An open source TCG Software Stack
 *
 * (C) Copyright International Business Machines Corp. 2004, 2005
 *
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "trousers/tss.h"
#include "trousers/trousers.h"
#include "spi_internal_types.h"
#include "spi_utils.h"
#include "capabilities.h"
#include "tsplog.h"
#include "obj.h"

TSS_RESULT
Tspi_Data_Bind(TSS_HENCDATA hEncData,	/*  in */
	       TSS_HKEY hEncKey,	/*  in */
	       UINT32 ulDataLength,	/*  in */
	       BYTE *rgbDataToBind	/*  in */
    )
{
	UINT32 encDataLength;
	BYTE encData[256];
	BYTE *keyData;
	UINT32 keyDataLength;
	TCPA_BOUND_DATA boundData;
	UINT16 offset;
	BYTE bdblob[256];
	TCPA_RESULT result;
	TCPA_KEY keyContainer;
	TSS_HCONTEXT tspContext;

	if (rgbDataToBind == NULL)
		return TSPERR(TSS_E_BAD_PARAMETER);

	if ((result = obj_rsakey_get_tsp_context(hEncKey, &tspContext)))
		return result;

	if ((result = obj_rsakey_get_blob(hEncKey, &keyDataLength, &keyData)))
		return result;

	offset = 0;
	if ((result = Trspi_UnloadBlob_KEY(tspContext, &offset, keyData,
					&keyContainer)))
		return result;

	if (keyContainer.keyUsage != TPM_KEY_BIND &&
		keyContainer.keyUsage != TPM_KEY_LEGACY) {
		return TSPERR(TSS_E_INVALID_KEYUSAGE);
	}

	if (keyContainer.algorithmParms.encScheme == TCPA_ES_RSAESPKCSv15 &&
	    keyContainer.keyUsage == TPM_KEY_LEGACY) {
		if ((result = Trspi_RSA_PKCS15_Encrypt(rgbDataToBind,
						ulDataLength,
						encData,
						&encDataLength,
						keyContainer.pubKey.key,
						keyContainer.pubKey.keyLength)))
			return result;
	} else if (keyContainer.algorithmParms.encScheme == TCPA_ES_RSAESPKCSv15
		   && keyContainer.keyUsage == TPM_KEY_BIND) {
		boundData.payload = TCPA_PT_BIND;

		memcpy(&boundData.ver, &VERSION_1_1, sizeof(TCPA_VERSION));

		boundData.payloadData = malloc(ulDataLength);
		if (boundData.payloadData == NULL) {
			return TSPERR(TSS_E_OUTOFMEMORY);
		}
		memcpy(boundData.payloadData, rgbDataToBind, ulDataLength);

		offset = 0;
		Trspi_LoadBlob_BOUND_DATA(&offset, boundData, ulDataLength, bdblob);

		if ((result = Trspi_RSA_PKCS15_Encrypt(bdblob,
						offset,
						encData,
						&encDataLength,
						keyContainer.pubKey.key,
						keyContainer.pubKey.keyLength)))
			return result;

		free(boundData.payloadData);
	} else {
		boundData.payload = TCPA_PT_BIND;

		memcpy(&boundData.ver, &VERSION_1_1, sizeof(TCPA_VERSION));

		boundData.payloadData = malloc(ulDataLength);
		if (boundData.payloadData == NULL) {
			LogError("malloc of %d bytes failed.", ulDataLength);
			return TSPERR(TSS_E_OUTOFMEMORY);
		}
		memcpy(boundData.payloadData, rgbDataToBind, ulDataLength);

		offset = 0;
		Trspi_LoadBlob_BOUND_DATA(&offset, boundData, ulDataLength,
						bdblob);

		if ((result = Trspi_RSA_Encrypt(bdblob, offset, encData,
					    &encDataLength,
					    keyContainer.pubKey.key,
					    keyContainer.pubKey.keyLength)))
			return result;

		free(boundData.payloadData);
	}

	if ((result = obj_encdata_set_data(hEncData, encDataLength, encData))) {
		LogError1("Error in calling SetAttribData on the encrypted "
				"data object.");
		return TSPERR(TSS_E_INTERNAL_ERROR);
	}

	return TSS_SUCCESS;
}

TSS_RESULT
Tspi_Data_Unbind(TSS_HENCDATA hEncData,		/*  in */
		 TSS_HKEY hKey,			/*  in */
		 UINT32 * pulUnboundDataLength,	/*  out */
		 BYTE ** prgbUnboundData	/*  out */
    )
{
	TCPA_RESULT result;
	TPM_AUTH privAuth;
	TCPA_DIGEST digest;
	UINT16 offset;
	TSS_HPOLICY hPolicy;
	BYTE *encData;
	UINT32 encDataSize;
	TCS_CONTEXT_HANDLE tcsContext;
	BYTE hashBlob[1024];
	TCS_KEY_HANDLE tcsKeyHandle;
	TSS_BOOL usesAuth;
	TPM_AUTH *pPrivAuth;
        TSS_HCONTEXT tspContext;

	if (pulUnboundDataLength == NULL || prgbUnboundData == NULL)
		return TSPERR(TSS_E_BAD_PARAMETER);

	if ((result = obj_encdata_get_tsp_context(hEncData, &tspContext)))
		return result;

	if ((result = obj_context_is_connected(tspContext, &tcsContext)))
		return result;

	if ((result = obj_rsakey_get_policy(hKey, TSS_POLICY_USAGE, &hPolicy, &usesAuth)))
		return result;

	if ((result = obj_encdata_get_data(hEncData, &encDataSize, &encData)))
		return result;

	tcsKeyHandle = getTCSKeyHandle(hKey);
	if (tcsKeyHandle == NULL_HKEY) {
		result = TSPERR(TSS_E_KEY_NOT_LOADED);
		return result;
	}

	if (usesAuth) {
		offset = 0;
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_UnBind, hashBlob);
		Trspi_LoadBlob_UINT32(&offset, encDataSize, hashBlob);
		Trspi_LoadBlob(&offset, encDataSize, hashBlob, encData);

		Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, digest.digest);

		if ((result = secret_PerformAuth_OIAP(hPolicy, &digest, &privAuth)))
			return result;
		pPrivAuth = &privAuth;
	} else {
		pPrivAuth = NULL;
	}

	if ((result = TCSP_UnBind(tcsContext, tcsKeyHandle, encDataSize,
				 encData, pPrivAuth, pulUnboundDataLength,
				 prgbUnboundData)))
		return result;

	if (usesAuth) {
		offset = 0;
		Trspi_LoadBlob_UINT32(&offset, result, hashBlob);
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_UnBind, hashBlob);
		Trspi_LoadBlob_UINT32(&offset, *pulUnboundDataLength, hashBlob);
		Trspi_LoadBlob(&offset, *pulUnboundDataLength, hashBlob,
				*prgbUnboundData);
		Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, digest.digest);

		if ((result = obj_policy_validate_auth_oiap(hPolicy, &digest,
						&privAuth))) {
			free_tspi(tspContext, *prgbUnboundData);
			return result;
		}
	}

	return TSS_SUCCESS;
}

TSS_RESULT
Tspi_Data_Seal(TSS_HENCDATA hEncData,	/*  in */
	       TSS_HKEY hEncKey,	/*  in */
	       UINT32 ulDataLength,	/*  in */
	       BYTE * rgbDataToSeal,	/*  in */
	       TSS_HPCRS hPcrComposite	/*  in */
    )
{
	UINT16 offset;
	BYTE hashBlob[0x1000];
	BYTE sharedSecret[20];
	TPM_AUTH auth;
	TCPA_ENCAUTH encAuthUsage;
	TCPA_ENCAUTH encAuthMig;
	TCPA_DIGEST digest;
	TCPA_RESULT result;
	TCS_CONTEXT_HANDLE tcsContext;
	TSS_HPOLICY hPolicy, hEncPolicy;
	BYTE *encData = NULL;
	UINT32 encDataSize;
	UINT32 pcrDataSize;
	BYTE pcrData[256];
	TCS_KEY_HANDLE tcsKeyHandle;
	TCPA_NONCE nonceEvenOSAP;
	TSS_BOOL useAuth;
	TPM_AUTH *pAuth;
	TCPA_DIGEST digAtCreation;
	TSS_HCONTEXT tspContext;
	TCPA_PCR_SELECTION pcrSelect = { 0, NULL };

	if (rgbDataToSeal == NULL)
		return TSPERR(TSS_E_BAD_PARAMETER);

	if ((result = obj_encdata_get_tsp_context(hEncData, &tspContext)))
		return result;

	if ((result = obj_context_is_connected(tspContext, &tcsContext)))
		return result;

	if ((result = obj_rsakey_get_policy(hEncKey, TSS_POLICY_USAGE,
					&hPolicy, &useAuth)))
		return result;

	if ((result = obj_encdata_get_data(hEncData, &encDataSize, &encData)))
		return result;

	if ((result = obj_encdata_get_policy(hEncData, TSS_POLICY_USAGE,
						&hEncPolicy)))
		return result;

	tcsKeyHandle = getTCSKeyHandle(hEncKey);
	if (tcsKeyHandle == NULL_HKEY)
		return TSPERR(TSS_E_KEY_NOT_LOADED);

	/* ---  If PCR's are of interest */
	pcrDataSize = 0;
	if (hPcrComposite) {
		/* XXX */
		if ((result = generateCompositeFromTPM(tcsContext,
						   &pcrSelect,
						   &digAtCreation)))
			return result;

		offset = 0;
		Trspi_LoadBlob_PCR_SELECTION(&offset, pcrData, &pcrSelect);
		Trspi_LoadBlob(&offset, TCPA_SHA1_160_HASH_LEN, pcrData,
				digAtCreation.digest);
		/* XXX */
		Trspi_LoadBlob(&offset, TCPA_SHA1_160_HASH_LEN, pcrData,
				digAtCreation.digest);
		pcrDataSize = offset;
	}

	if ((result = secret_PerformXOR_OSAP(hPolicy, hEncPolicy, hEncPolicy,
					hEncKey, TCPA_ET_KEYHANDLE,
					tcsKeyHandle,
					&encAuthUsage, &encAuthMig,
					sharedSecret, &auth,
					&nonceEvenOSAP)))
		return result;

	if (useAuth) {
		offset = 0;
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_Seal, hashBlob);
		Trspi_LoadBlob(&offset, 20, hashBlob, encAuthUsage.authdata);
		Trspi_LoadBlob_UINT32(&offset, pcrDataSize, hashBlob);
		Trspi_LoadBlob(&offset, pcrDataSize, hashBlob, pcrData);
		Trspi_LoadBlob_UINT32(&offset, ulDataLength, hashBlob);
		Trspi_LoadBlob(&offset, ulDataLength, hashBlob, rgbDataToSeal);
		Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, digest.digest);
		pAuth = &auth;
	} else {
		pAuth = NULL;
	}

	if ((result = secret_PerformAuth_OSAP(hPolicy, hEncPolicy, hEncPolicy,
					  hEncKey, sharedSecret, &auth,
					  digest.digest, &nonceEvenOSAP)))
		return result;

	if ((result = TCSP_Seal(tcsContext, tcsKeyHandle, encAuthUsage,
			    pcrDataSize, pcrData, ulDataLength, rgbDataToSeal,
			    pAuth, &encDataSize, &encData)))
		return result;

	if (useAuth) {
		offset = 0;
		Trspi_LoadBlob_UINT32(&offset, result, hashBlob);
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_Seal, hashBlob);
		Trspi_LoadBlob(&offset, encDataSize, hashBlob, encData);
		Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, digest.digest);

		if ((result = secret_ValidateAuth_OSAP(hPolicy, hEncPolicy,
						   hEncPolicy, sharedSecret,
						   &auth, digest.digest,
						   &nonceEvenOSAP))) {
			free(encData);
			return result;
		}
	}

	/* Need to set the object with the blob and the pcr's */
	if ((result = obj_encdata_set_data(hEncData, encDataSize, encData)))
		return result;

	free(encData);

	if (pcrDataSize) {
		if ((result = obj_encdata_set_pcr_info(hEncData, pcrData)))
			return result;
	}

	return TSS_SUCCESS;
}

TSS_RESULT
Tspi_Data_Unseal(TSS_HENCDATA hEncData,		/*  in */
		 TSS_HKEY hKey,			/*  in */
		 UINT32 * pulUnsealedDataLength,/*  out */
		 BYTE ** prgbUnsealedData	/*  out */
    )
{
	TPM_AUTH privAuth, privAuth2;
	TPM_AUTH *pPrivAuth;
	UINT16 offset;
	BYTE hashblob[0x400];
	TCPA_DIGEST digest;
	TCPA_RESULT result;
	TCS_CONTEXT_HANDLE tcsContext;
	TSS_HPOLICY hPolicy, hEncPolicy;
	TCS_KEY_HANDLE tcsKeyHandle;
	TSS_BOOL useAuth;
        TSS_HCONTEXT tspContext;
	UINT32 ulDataLen;
	BYTE *data;

	if (pulUnsealedDataLength == NULL || prgbUnsealedData == NULL)
		return TSPERR(TSS_E_BAD_PARAMETER);

	if ((result = obj_encdata_get_tsp_context(hEncData, &tspContext)))
		return result;

	if ((result = obj_context_is_connected(tspContext, &tcsContext)))
		return result;

	if ((result = obj_rsakey_get_policy(hKey, TSS_POLICY_USAGE,
					&hPolicy, &useAuth)))
		return result;

	if ((result = obj_encdata_get_policy(hEncData, TSS_POLICY_USAGE,
					&hEncPolicy)))
		return result;

	if ((result = obj_encdata_get_data(hEncData, &ulDataLen, &data)))
		return result;

	tcsKeyHandle = getTCSKeyHandle(hKey);
	if (tcsKeyHandle == NULL_HKEY)
		return TSPERR(TSS_E_KEY_NOT_LOADED);

	offset = 0;
	Trspi_LoadBlob_UINT32(&offset, TPM_ORD_Unseal, hashblob);
	Trspi_LoadBlob(&offset, ulDataLen, hashblob, data);
	Trspi_Hash(TSS_HASH_SHA1, offset, hashblob, digest.digest);
	if (useAuth) {
		if ((result = secret_PerformAuth_OIAP(hPolicy, &digest,
							&privAuth)))
			return result;
		pPrivAuth = &privAuth;
	} else {
		pPrivAuth = NULL;
	}

	if ((result = secret_PerformAuth_OIAP(hEncPolicy, &digest,
					&privAuth2))) {
		if (useAuth)
			TCSP_TerminateHandle(tcsContext, privAuth.AuthHandle);
		return result;
	}

	if ((result = TCSP_Unseal(tcsContext, tcsKeyHandle,
				  ulDataLen, data, pPrivAuth,
				  &privAuth2, pulUnsealedDataLength,
				  prgbUnsealedData)))
		return result;

	offset = 0;
	Trspi_LoadBlob_UINT32(&offset, result, hashblob);
	Trspi_LoadBlob_UINT32(&offset, TPM_ORD_Unseal, hashblob);
	Trspi_LoadBlob_UINT32(&offset, *pulUnsealedDataLength, hashblob);
	Trspi_LoadBlob(&offset, *pulUnsealedDataLength, hashblob,
			*prgbUnsealedData);
	Trspi_Hash(TSS_HASH_SHA1, offset, hashblob, digest.digest);
	if (useAuth) {
		if ((result = obj_policy_validate_auth_oiap(hPolicy, &digest,
							&privAuth))) {
			free_tspi(tspContext, *prgbUnsealedData);
			return result;
		}
	}

	if ((result = obj_policy_validate_auth_oiap(hEncPolicy, &digest,
					&privAuth2))) {
		free_tspi(tspContext, *prgbUnsealedData);
		return result;
	}

	return TSS_SUCCESS;
}
