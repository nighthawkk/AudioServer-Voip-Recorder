//
// Created by user on 10-01-2023.
//
#ifndef GET_LIB_ADDRESS_H
#define GET_LIB_ADDRESS_H

#ifdef __cplusplus
extern "C" {
#endif

uint64_t hexToInt64(const char *str);
uint64_t GetSymbolAddress(char* lib_path, char* symbol_name);
uint64_t GetLibAddress(char* lib_name);

#ifdef __cplusplus
}; /* extern "C" */
#endif

#endif //GET_LIB_ADDRESS_H

