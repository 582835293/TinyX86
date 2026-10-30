#define _CRT_SECURE_NO_WARNINGS
#include "PETools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

uint32_t ReadPEFile(const char* filepath, void** ppFileBuffer) {
    if (!filepath || !ppFileBuffer) return 0;
    *ppFileBuffer = NULL;
    FILE* fp = fopen(filepath, "rb");
    if (!fp) return 0;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    long sz = ftell(fp);
    if (sz <= 0) {
        fclose(fp);
        return 0;
    }
    rewind(fp);

    void* buf = malloc((size_t)sz);
    if (!buf) {
        fclose(fp);
        return 0;
    }
    size_t read_bytes = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    if (read_bytes != (size_t)sz) {
        free(buf);
        return 0;
    }
    *ppFileBuffer = buf;
    return (uint32_t)sz;
}

bool ParsePEHeaders(void* pFileBuffer, PEHeaderInfo* pInfo) {
    if (!pFileBuffer || !pInfo) return false;
    memset(pInfo, 0, sizeof(*pInfo));

    PIMAGE_DOS_HEADER pDos = (PIMAGE_DOS_HEADER)pFileBuffer;
    if (pDos->e_magic != 0x5A4D) { /* 'MZ' */
        return false;
    }

    PIMAGE_NT_HEADERS32 pNt = (PIMAGE_NT_HEADERS32)((uint8_t*)pFileBuffer + pDos->e_lfanew);
    if (pNt->Signature != 0x00004550) { /* 'PE\0\0' */
        return false;
    }

    pInfo->wNumberOfSections = pNt->FileHeader.NumberOfSections;
    pInfo->dwEntryPoint      = pNt->OptionalHeader.AddressOfEntryPoint;
    pInfo->dwImageBase       = pNt->OptionalHeader.ImageBase;
    pInfo->dwSizeOfImage     = pNt->OptionalHeader.SizeOfImage;
    pInfo->dwSizeOfHeaders   = pNt->OptionalHeader.SizeOfHeaders;
    return true;
}
