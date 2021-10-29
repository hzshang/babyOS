/*
 * physical_page.c
 * Copyright (C) 2021 mac <hzshang15@gmail.com>
 *
 * Distributed under terms of the MIT license.
 */
#include <types.h>
#include <libcc.h>
#include <physical_page.h>
static uint8_t* page_ptr;

uint8_t* physical_alloc(uint32_t size){
    size = (size+0xfff)&(~0xfff);
    uint8_t* page = page_ptr;
    page_ptr += size;
    memset(page,0,size);
    return page;
}


void physical_page_init(uint8_t* page,uint32_t size){
//    uint32_t pool_size = size >> (12 + 3);
//    page_pool = create_bitmap(pool_size);
    page_ptr = page;
}







