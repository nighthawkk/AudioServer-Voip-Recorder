#include <jni.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <elf.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <android/log.h>

#include "../include/get_lib_address.h"

#define  logger(...)  __android_log_print(ANDROID_LOG_VERBOSE, "AudioHook", __VA_ARGS__)

uint64_t hexToInt64(const char *str)
{
    uint64_t res = 0;
    char c;

    while ((c = *str++)) {
        char v = (c & 0xF) + (c >> 6) | ((c >> 3) & 0x8);
        res = (res << 4) | (uint64_t) v;
    }

    // logger(res);
    logger("res %p", res);

    return res;
}

uint64_t GetSymbolAddress(char* lib_path, char* symbol_name) {
    int fd = open(lib_path, O_RDONLY);
    if (fd < 0) return 0;

    struct stat st;
    fstat(fd, &st);
    uint8_t* data = (uint8_t*)mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (!data) return 0;

    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)data;
    Elf64_Shdr* shdr = (Elf64_Shdr*)(data + ehdr->e_shoff);

    char* sh_strtab = (char*)(data + shdr[ehdr->e_shstrndx].sh_offset);
    char* dynstr = NULL;
    char* strtab = NULL;

    // find .strtab and .dynstr
    for (int i = 0; i < ehdr->e_shnum; i++) {
        const char* name = sh_strtab + shdr[i].sh_name;
        if (shdr[i].sh_type == SHT_STRTAB) {
            if (!strcmp(name, ".strtab")) strtab = (char*)(data + shdr[i].sh_offset);
            else if (!strcmp(name, ".dynstr")) dynstr = (char*)(data + shdr[i].sh_offset);
        }
    }

    uint64_t ph_vaddr = 0;
    Elf64_Phdr* phdr = (Elf64_Phdr*)(data + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            ph_vaddr = phdr[i].p_vaddr;
            break;
        }
    }

    uint64_t module_base = GetLibAddress(lib_path);
    if (!module_base) return 0;

    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type != SHT_SYMTAB && shdr[i].sh_type != SHT_DYNSYM) continue;

        Elf64_Sym* syms = (Elf64_Sym*)(data + shdr[i].sh_offset);
        int count = shdr[i].sh_size / sizeof(Elf64_Sym);
        char* sym_names = (shdr[i].sh_type == SHT_SYMTAB) ? strtab : dynstr;

        for (int j = 0; j < count; j++) {
            const char* name = sym_names + syms[j].st_name;
            if (name && !strcmp(name, symbol_name)) {
                uint64_t addr = module_base + syms[j].st_value - ph_vaddr;
                munmap(data, st.st_size);
                return addr;
            }
        }
    }

    munmap(data, st.st_size);
    return 0;
}

// uint64_t GetSymbolAddress(char* lib_path, char* symbol_name)
// {
//     uint8_t* lib_address_in_mem = 0;

//     int fd, i;
//     struct stat st;
//     fd = openat( AT_FDCWD,lib_path, O_RDONLY,S_IRWXU);
//     fstat(fd, &st);
//     uint8_t* m_mmap_program = (uint8_t*)mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
//     close(fd);
//     uint8_t* lib_address = m_mmap_program;
//     Elf64_Ehdr *ehdr = (Elf64_Ehdr*)lib_address;
//     Elf64_Shdr *shdr = (Elf64_Shdr*)(lib_address + ehdr->e_shoff);

//     Elf64_Phdr *phdr = (Elf64_Phdr*)(lib_address + ehdr->e_phoff);

//     Elf64_Ehdr* header = (Elf64_Ehdr*)m_mmap_program;

//     char *_sh_strtab_p = NULL;
//     char *sh_dynstr_p = NULL;

//     int shnum = ehdr->e_shnum;
//     int phnum = ehdr->e_phnum;


//     uint64_t Vaddr;


//     for (int i = 0; i < phnum; ++i) {
//         if(phdr[i].p_type == 1){
//             Vaddr = phdr[i].p_vaddr;
//             break; // break at first find
//             }
//     }


//     Elf64_Shdr *sh_strtab = (Elf64_Shdr*)&shdr[ehdr->e_shstrndx];

//     const char *const sh_strtab_p = (char*)(lib_address + (sh_strtab->sh_offset));

//     for (int i = 0; i < shnum; ++i) {
//         if(_sh_strtab_p == NULL && ((shdr[i].sh_type == 3) && !strcmp(sh_strtab_p + shdr[i].sh_name, ".strtab"))){
//             _sh_strtab_p = (char*)lib_address + shdr[i].sh_offset;
//         }
//         if(sh_dynstr_p == NULL && ((shdr[i].sh_type == 3) && !strcmp(sh_strtab_p + shdr[i].sh_name,".dynstr"))){
//             sh_dynstr_p = (char*)lib_address + shdr[i].sh_offset;
//         }

//         if(sh_dynstr_p != NULL && _sh_strtab_p != NULL)
//             break;
//     }
//     for (int i = 0; i < shnum; ++i) {

//         if((shdr[i].sh_type != 2) && (shdr[i].sh_type != 11))
//             continue;
//         unsigned int total_syms = shdr[i].sh_size / sizeof(Elf64_Sym);
//         Elf64_Sym* syms_data = (Elf64_Sym*)(lib_address + shdr[i].sh_offset);

//         for (int j = 0; j < total_syms; ++j) {
//             char *sym_name = NULL;
//             if(shdr[i].sh_type == 2)
//             {
//                 sym_name = _sh_strtab_p + syms_data[j].st_name;
//             }
//             if(shdr[i].sh_type == 11)
//             {
//                 sym_name = sh_dynstr_p + syms_data[j].st_name;
//             }
//             logger("symbol_name %s", sym_name);
//             if(strstr("mJavaVM",symbol_name))
//             {
// 	            if(strstr(sym_name, "mJavaVM") && strstr(sym_name, "android") && strstr(sym_name, "AndroidRuntime"))
// 			    {
// 	                uint64_t ss = syms_data[j].st_value - Vaddr;
//                     logger( "st_value %llx", ss);
// 	                return ss;
// 	            }
//         	}
//         	else{
//         		if(!strcmp(sym_name, symbol_name))
// 	            {
// 	                return (long)lib_address_in_mem + syms_data[j].st_value - Vaddr; //found
// 	            }
//         	}
//         }
//     }
//     return 0;
// }

uint64_t GetLibAddress(char* lib_name)
{
    int fd = openat(AT_FDCWD,"/proc/self/maps",O_RDONLY,S_IRWXU);
    const int BUFFER_SIZE = 512;
    char buffer;
    char line_buffer[BUFFER_SIZE];
    int n=0;

    while(read(fd,&buffer,1))
    {
        if(buffer == '\n')
        {
            line_buffer[n] = 0;
            n=0;

            if(strstr(line_buffer,lib_name))
            {
                // logger(line_buffer);
                // found our lib
                unsigned int dash_pos = strstr(line_buffer,"-") - line_buffer;
                line_buffer[dash_pos] = 0;
                close(fd);
                return hexToInt64(line_buffer);
            }
        }
        else
        {
            line_buffer[n++] = buffer;
        }
    }
    close(fd);
    return 0;
}
