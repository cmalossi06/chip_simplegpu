/*
 ============================================================================
 Name        : main.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello RISC-V World in C
 ============================================================================
 */

#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <platform.h>
#include "init.h"

#define STRBUF_SIZE			256	// String buffer size


int main(void)
{
	uint32_t reg_value;
	uint32_t i = 0;
	_init();

	while(1)
	{
		reg_value = MY_PERIPH_REG(MY_PERIPH_REG_IO);
		printf("my_periph_reg=0x%08x \n",reg_value);
		MY_PERIPH_REG(MY_PERIPH_REG_IO) = i++;
	}
	return 0;
}
