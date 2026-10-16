/* nativefiledialog-extended for the Nintendo Switch, which has no file dialogs: every dialog
   reports an error. The ROM is read from a fixed place on the SD card instead (main.cpp), and
   mods are copied into the mods folder there by hand. */

#include <stddef.h>

#include "nfd.h"

static const char* const no_dialogs = "There are no file dialogs on the Switch.";

nfdresult_t NFD_Init(void) {
    return NFD_OKAY;
}

void NFD_Quit(void) {
}

void NFD_FreePathN(nfdnchar_t* filePath) {
    (void)filePath;
}

nfdresult_t NFD_OpenDialogN(nfdnchar_t** outPath, const nfdnfilteritem_t* filterList,
                            nfdfiltersize_t filterCount, const nfdnchar_t* defaultPath) {
    (void)filterList; (void)filterCount; (void)defaultPath;
    *outPath = NULL;
    return NFD_ERROR;
}

nfdresult_t NFD_OpenDialogMultipleN(const nfdpathset_t** outPaths, const nfdnfilteritem_t* filterList,
                                    nfdfiltersize_t filterCount, const nfdnchar_t* defaultPath) {
    (void)filterList; (void)filterCount; (void)defaultPath;
    *outPaths = NULL;
    return NFD_ERROR;
}

nfdresult_t NFD_SaveDialogN(nfdnchar_t** outPath, const nfdnfilteritem_t* filterList,
                            nfdfiltersize_t filterCount, const nfdnchar_t* defaultPath,
                            const nfdnchar_t* defaultName) {
    (void)filterList; (void)filterCount; (void)defaultPath; (void)defaultName;
    *outPath = NULL;
    return NFD_ERROR;
}

nfdresult_t NFD_PickFolderN(nfdnchar_t** outPath, const nfdnchar_t* defaultPath) {
    (void)defaultPath;
    *outPath = NULL;
    return NFD_ERROR;
}

const char* NFD_GetError(void) {
    return no_dialogs;
}

void NFD_ClearError(void) {
}

nfdresult_t NFD_PathSet_GetCount(const nfdpathset_t* pathSet, nfdpathsetsize_t* count) {
    (void)pathSet;
    *count = 0;
    return NFD_ERROR;
}

nfdresult_t NFD_PathSet_GetPathN(const nfdpathset_t* pathSet, nfdpathsetsize_t index, nfdnchar_t** outPath) {
    (void)pathSet; (void)index;
    *outPath = NULL;
    return NFD_ERROR;
}

void NFD_PathSet_FreePathN(const nfdnchar_t* filePath) {
    (void)filePath;
}

void NFD_PathSet_Free(const nfdpathset_t* pathSet) {
    (void)pathSet;
}
