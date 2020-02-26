#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include "at_driver.h"
#include "error.h"
#include "delay.h"
#include "uart.h"
#include "irq.h"
//#include "list.h"

/** Length of AT+CIPSTART */
#define MAX_CMD_LEN	11u

/* 100 miliseconds */
#define MODULE_TIMEOUT		10000


struct cmd_desc {
	struct at_buff	cmd;
	uint32_t	type;
};

const static struct at_buff	ok_resposen = {(uint8_t *)"\r\nOK\r\n", 6u};
const static struct at_buff	error_resposen = {(uint8_t *)"\r\nERROR\r\n",
									    9u};
const static struct at_buff	*responses[2] = {&ok_resposen, &error_resposen};

static uint32_t			initialized;

static const struct cmd_desc g_map[] = {
/*00*/	{{(uint8_t *)"", 0}, AT_EXECUTE_OP},
/*01*/	{{(uint8_t *)"+RST", 4}, AT_EXECUTE_OP},
/*02*/	{{(uint8_t *)"+GSLP", 5}, AT_SET_OP},
/*03*/	{{(uint8_t *)"+GMR", 4}, AT_EXECUTE_OP},
/*04*/	{{(uint8_t *)"+CWMODE", 7}, AT_QUERY_OP | AT_SET_OP | AT_TEST_OP},
/*05*/	{{(uint8_t *)"+CWJAP", 6}, AT_QUERY_OP | AT_SET_OP},
/*06*/	{{(uint8_t *)"+CWLAP", 6}, AT_EXECUTE_OP},
/*07*/	{{(uint8_t *)"+CWQAP", 6}, AT_EXECUTE_OP},
/*08*/	{{(uint8_t *)"+CWSAP", 6}, AT_QUERY_OP | AT_SET_OP},
/*09*/	{{(uint8_t *)"+CWLIF", 6}, AT_EXECUTE_OP},
/*10*/	{{(uint8_t *)"+CIPSTATUS", 10}, AT_EXECUTE_OP},
/*11*/	{{(uint8_t *)"+CIPSTART", 9}, AT_TEST_OP | AT_SET_OP},
/*12*/	{{(uint8_t *)"+CIPSEND", 8}, AT_SET_OP},
/*13*/	{{(uint8_t *)"+CIPCLOSE", 9}, AT_EXECUTE_OP},
/*14*/	{{(uint8_t *)"+CIFSR", 6}, AT_EXECUTE_OP},
/*15*/	{{(uint8_t *)"+CIPMUX", 7}, AT_QUERY_OP | AT_SET_OP},
/*16*/	{{(uint8_t *)"+CIPSERVER", 10}, AT_SET_OP},
/*17*/	{{(uint8_t *)"+CIPMODE", 8}, AT_QUERY_OP | AT_SET_OP},
/*18*/	{{(uint8_t *)"+CIPSTO", 7}, AT_QUERY_OP | AT_SET_OP},
/*19*/	{{(uint8_t *)"+PING", 5}, AT_SET_OP}
};

#include <ctype.h>
void trap();

#define CIFSR_FORMAT "+CIFSR:STAIP,\"%s\"\r\n"\
		     "+CIFSR:STAMAC,\"%s\"\r\n"
/* Only one param */
static inline void at_sscanf(struct at_buff *src, char *format, void *result,
				struct at_buff **next);
static inline void at_strcpy(struct at_buff *dest, const struct at_buff *src);
static inline void at_strcat(struct at_buff *dest, const struct at_buff *src);
static inline void at_set(struct at_buff *dest, uint8_t *buff, uint32_t len);
static inline void at_strcatch(struct at_buff *dest, uint8_t ch);

void printReadBuff(struct at_desc *desc);

/* this should be provided by the user */
static void enable_rx(struct at_desc *desc, uint32_t enable) {
	uint32_t UART0_RX_PORTP0_MUX = (uint32_t)1<<22;
	if (enable)
		*((volatile uint32_t *)REG_GPIO0_CFG) &= ~UART0_RX_PORTP0_MUX;
	else
		*((volatile uint32_t *)REG_GPIO0_CFG) |= UART0_RX_PORTP0_MUX;
}

static inline void refresh_status(struct at_desc *desc)
{
	uint8_t		last_char = desc->result.buff[desc->result.len - 1];
	uint32_t	i;

	for (i = 0; i < 2; i++) {
		if (desc->check_idx[i] > responses[i]->len) {
			printf("Driver implementation error \n");
			return ;
		}
		/*
		 * Check if the last read char match the current index on the
		 * response.
		 */
		if (last_char == responses[i]->buff[desc->check_idx[i]]) {
			desc->check_idx[i]++;
			if (desc->check_idx[i] == responses[i]->len) {
				/* One response match */
				desc->result.len -= desc->check_idx[i];
				desc->result_status = (i == 0) ?
						RESULT_AVAILABE : RESULT_ERROR;
				desc->check_idx[0] = 0;
				desc->check_idx[1] = 0;
				return ;
			}
		}
		else if (last_char == responses[i]->buff[0]){
			desc->check_idx[i] = 1;
		}
		else {
			desc->check_idx[i] = 0;
		}
	}
}

static void at_callback(void *app_param, enum UART_EVENT event, uint8_t	*data)
{
	struct at_desc *desc = app_param;
	switch (event)
	{
	case WRITE_DONE:
		desc->cmd.len = 0;
		break;
	case READ_DONE:
		if (desc->result.len < RESULT_BUFF_LEN)
		{
			desc->buffers.result_buff[desc->result.len] =
								*desc->read_ch;
			desc->result.len++;
			refresh_status(desc);
		}
		else
			desc->result_status = OVERFLOW_ERROR;
		uart_read(desc->uart_desc, desc->read_ch, 1);
		break;
	case ERROR:
		printf("error %x\n", (unsigned)data);
		break;
	default:
		break;
	}
}

/**
 * Wait the response for the last command with a timeout of \ref MODULE_TIMEOUT
 * milliseconds.
 * @param desc
 */
static inline void wait_for_response(struct at_desc *desc)
{
	uint32_t counter;

	timer_start(desc->timer);
	do {
		if (desc->result_status != WAITING_RESULT)
			break;
		timer_counter_get(desc->timer, &counter);
	} while (counter < MODULE_TIMEOUT);
	timer_stop(desc->timer);
}

static void at_write_cmd(struct at_desc *desc)
{
	/* Mark result buffer as read */
	// think about syncronization for this
	desc->result.len = 0;
	desc->result_status = WAITING_RESULT;

	uart_write(desc->uart_desc, desc->cmd.buff, desc->cmd.len);
	while (desc->cmd.len)
		/* Len will be set to 0 when write is done */
		;
}

static int32_t stop_echo(struct at_desc *desc)
{
	memcpy(desc->cmd.buff, "ATE0\r\n", 6);
	desc->cmd.len = 6;
	at_write_cmd(desc);

	wait_for_response(desc);

	if (desc->result_status == RESULT_AVAILABE)
		return SUCCESS;

	/* Timeout or error response */
	return FAILURE;
}

static int32_t handle_special(struct at_desc *desc, enum at_cmd cmd)
{
	switch (cmd) {
	case AT_RESET:
		/* Disable rx */
		enable_rx(desc, 0);

		at_write_cmd(desc);
		mdelay(5000);
		//printReadBuff(desc);

		/* Enable rx */
		enable_rx(desc, 1);

		//Rest callback statuses
		//?
		break;
	case AT_DEEP_SLEEP:
		//Not implemented
		return FAILURE;
	default:
		return FAILURE;
	}
	return SUCCESS;

}

static int32_t parse_result(struct at_desc *desc, enum at_cmd cmd,
		union out_param *result)
{
	at_strcpy(&desc->app_response, &desc->result);
	if (!result){
		printf("BUG: Check result in upper level\n");
		return FAILURE;
	}
	switch (cmd)
	{
	/* Not need for parsing */
	case AT_ATTENTION:
	case AT_RESET:
	case AT_DISCONNECT_NETWORK:
	case AT_STOP_CONNECTION://?
		return SUCCESS;
	/* Need the result to be parsed */

	/* Test commands */
	//case AT_SET_OPERATION_MODE: //Not parse need
	case AT_START_CONNECTION:   //Not parse need

	/* Querry commands */
	case AT_CONNECT_NETWORK:
	case AT_SET_ACCESS_POINT:
	case AT_SET_CONNECTION_TYPE:
	case AT_SET_TRANSPORT_MODE:
	case AT_SET_CLIENT_TIMEOUT:
	case AT_SET_OPERATION_MODE:
	/*TODO PARSE*/
	/*Exec commands */
	case AT_GET_AVAILABLE_NETWORKS:
	case AT_GET_STATUS:
	case AT_GET_IP:
	case AT_GET_VERSION: //Get version doesent need parsing
		//Copy received buffer to user buffer without parsing
		at_set(&result->not_parsed, desc->app_response.buff,
				desc->app_response.len);
		break;

	//TODO add the rest of the cases
	default:
		return FAILURE;
	}
	return SUCCESS;
}

static void at_printf(struct at_buff *dest, uint8_t *fmt, ...){
	    va_list args;
	    uint8_t		buff[12]; //A MAX int32_t fits
	    struct at_buff	aux_str[1];
	    struct at_buff	*str;
	    uint32_t		i;
	    int32_t		nb = 0;

	    dest->len = 0;
	    va_start (args, fmt);
	    while (*fmt)
	    {
		    switch (*fmt)
		    {
		    case 'd':
			    nb = va_arg(args, int32_t);
			    itoa(nb, (char *)buff, 10);
			    str_to_at(aux_str, buff);
			    at_strcat(dest, aux_str);
			    break;
		    case 's':
			    str = va_arg(args, struct at_buff *);
			    at_strcatch(dest, '\"');
			    /* Escape character */
			    for (i = 0; i < str->len; i++) {
				    uint8_t ch = str->buff[i];
				    if (ch == '\"' || ch == ',' || ch == '\\')
					    at_strcatch(dest, '\\');
				    at_strcatch(dest, ch);
			    }
			    at_strcatch(dest, '\"');
			    break;
		    default:
			    break;
		    }
		    fmt++;
		    if (*fmt)
			    at_strcatch(dest, ',');
	    }

	    va_end (args);
}

static void copy_param_to_cmd(struct at_desc *desc, enum at_cmd id,
						union in_param *param)
{

#define FORMAT_GSLP	((uint8_t *)"d")
#define FORMAT_CWMODE	((uint8_t *)"d")
#define FORMAT_CWJAP	((uint8_t *)"ss")

	switch (id){
	case AT_DEEP_SLEEP:
		at_printf(&desc->app_response, FORMAT_GSLP,
				param->deep_sleep_time_ms);
		break;
	case AT_SET_OPERATION_MODE:
		at_printf(&desc->app_response, FORMAT_CWMODE,
				param->wifi_mode);
		break;

	case AT_CONNECT_NETWORK:
		at_printf(&desc->app_response, FORMAT_CWJAP,
				&param->network.ssid, &param->network.pwd);
		break;
	case AT_SET_ACCESS_POINT:
	case AT_START_CONNECTION:
	case AT_SEND:
	case AT_SET_CONNECTION_TYPE:
	case AT_SET_SERVER:
	case AT_SET_TRANSPORT_MODE:
	case AT_SET_CLIENT_TIMEOUT:
	case AT_PING:
	default:
		break;
	}
	at_strcat(&desc->cmd, &desc->app_response);
}

/**
 *
 * @param desc
 * @param cmd
 * @param result Data will be valid until next call to a driver function
 * @return
 */
int32_t at_run_cmd(struct at_desc *desc, enum at_cmd cmd, enum cmd_operation op,
		union in_out_param *param)
{
	if (!desc)
		return FAILURE;

	if (!(g_map[cmd].type & op))
		return FAILURE;

	/* Write command in buffer: AT[CMD][OP]<parmas>\r\n*/
	/* AT */
	at_strcatch(&desc->cmd, 'A');
	at_strcatch(&desc->cmd, 'T');
	/* CMD */
	at_strcat(&desc->cmd, &g_map[cmd].cmd);
	/* OP */
	if (op == AT_QUERY_OP)
		at_strcatch(&desc->cmd, '?');
	if (op == AT_TEST_OP) {
		at_strcatch(&desc->cmd, '=');
		at_strcatch(&desc->cmd, '?');
	}
	if (op == AT_SET_OP && param) {
		at_strcatch(&desc->cmd, '=');
		copy_param_to_cmd(desc, cmd, &param->in);
	}
	/* \r\n */
	at_strcatch(&desc->cmd, '\r');
	at_strcatch(&desc->cmd, '\n');


	if (cmd == AT_DEEP_SLEEP || cmd == AT_RESET)
		return handle_special(desc, cmd);

	at_write_cmd(desc);
	wait_for_response(desc);
	if (desc->result_status != RESULT_AVAILABE)
		return FAILURE;

	if (param && op != AT_SET_OP)
		if (SUCCESS != parse_result(desc, cmd, &param->out))
			return FAILURE;
	//TODO Handle AT_DISCONNECT_NETWORK
	//Also test AT_STOP_CONNECTION
	return SUCCESS;
}


struct aducm_uart_init_param aducm_param = {
		.parity = UART_NO_PARITY,
		.stop_bits = UART_ONE_STOPBIT,
		.word_length = UART_WORDLEN_8BITS,
		.callback = at_callback,
		.param = NULL
};

struct uart_init_param uart_param = {0, BD_115200, &aducm_param};

struct timer_init_param timer_param = {0, 1000, 0, NULL};

/**
 *
 * @param desc
 * @param param
 */
int32_t at_init(struct at_desc **desc, struct at_init_param *param)
{
	struct at_desc	*ldesc;

	if (!desc || !param || !param->read_buff || !param->buff_size ||
			initialized)
		return FAILURE;

	ldesc = calloc(1, sizeof(*ldesc));
	if (!ldesc)
		return FAILURE;

	aducm_param.param = ldesc;
	if (SUCCESS != uart_init(&(ldesc->uart_desc), &uart_param))
		goto free_desc;
	/* The read will be handled by the callback */
	uart_read(ldesc->uart_desc, ldesc->read_ch, 1);

	if (SUCCESS != timer_init(&(ldesc->timer), &timer_param))
		goto free_uart;

	ldesc->received_package.buff = param->read_buff;
	ldesc->received_package.len = param->buff_size;
	/* Link buffer structure with static buffers */
	ldesc->app_response.buff = ldesc->buffers.app_response_buff;
	ldesc->app_response.len = RESULT_BUFF_LEN;
	ldesc->result.buff = ldesc->buffers.result_buff;
	ldesc->result.len = RESULT_BUFF_LEN;
	ldesc->cmd.buff = ldesc->buffers.cmd_buff;
	ldesc->cmd.len = CMD_BUFF_LEN;

	if (SUCCESS != stop_echo(ldesc))
		goto free_timer;

	/* Test AT */
	if (SUCCESS != at_run_cmd(ldesc, AT_ATTENTION, AT_EXECUTE_OP, NULL))
		goto free_timer;

	/* Deactivate echo */
	initialized = 1;
	*desc = ldesc;
	return SUCCESS;

free_timer:
	timer_remove(ldesc->timer);
free_uart:
	uart_remove(ldesc->uart_desc);
free_desc:
	free(ldesc);
	*desc = NULL;
	return FAILURE;
}

int32_t at_remove(struct at_desc *desc)
{
	if (!desc)
		return FAILURE;
	timer_remove(desc->timer);
	uart_remove(desc->uart_desc);
	free(desc);
	initialized = 0;

	return SUCCESS;
}

void str_to_at(struct at_buff *dest, uint8_t *src)
{
	if (!dest || !src)
		return ;

	dest->buff = src;
	dest->len = strlen((char *)src);
}

/*
uint8_t	*at_to_str(struct at_buff *src)
{
	if (!src)
		return NULL;
	src->buff[src->len] = 0;

	return src->buff;
}
*/

void trap(){
	printf("TRAP\n");
	//while (1);
}

/* At_buff functions */
static inline void at_strcpy(struct at_buff *dest, const struct at_buff *src)
{
	memcpy(dest->buff, src->buff, src->len);
	dest->len = src->len;
}

static inline void at_set(struct at_buff *dest, uint8_t *buff, uint32_t len)
{
	dest->buff = buff;
	dest->len = len;
}


static inline void at_strcat(struct at_buff *dest, const struct at_buff *src)
{
	memcpy(dest->buff + dest->len, src->buff, src->len);
	dest->len += src->len;
}

static inline void at_strcatch(struct at_buff *dest, uint8_t ch) {
	dest->buff[dest->len] = ch;
	dest->len++;
}

static inline void at_sscanf(struct at_buff *src, char *format, void *result,
				struct at_buff **next){
	//TODO
}

void printReadBuff(struct at_desc *desc){
	printf("Read buff:");
	for (int i = 0; i < desc->result.len; i++)
	{
		uint8_t ch = desc->result.buff[i];
		if (isprint(ch))
			printf("%c", ch);
		else if (ch == '\r')
			printf("\\r");
		else if (ch == '\n')
			printf("\\n");
		else
			printf("0x%02x", ch);
	}
	printf("\n");
}
