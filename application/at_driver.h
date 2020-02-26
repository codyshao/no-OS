/** https://cdn.sparkfun.com/datasheets/Wireless/WiFi/Command%20Doc.pdf */
/** https://github.com/espressif/ESP8266_AT/wiki/basic_at_0019000902
 * think about add at+GSLP
 * */
#ifndef AT_DRIVER_H
# define AT_DRIVER_H

#include  <stdint.h>
#include "uart.h"
#include "uart_extra.h"
#include "timer.h"

//#define PARSE_RESULT
/**
 *
 */
enum at_cmd{
	/** Attention */
/*00*/	AT_ATTENTION,
	/** Reset module */
	//Must stop reading when using this -> special case
/*01*/	AT_RESET,
	/** Enter in deep sleep mode */
/*02*/	AT_DEEP_SLEEP,
	/** View version info */
/*03*/	AT_GET_VERSION,
	/**
	 * Setting operation mode.
	 * - Client
	 * - Access Point
	 * - Client and Access Point
	 * The parameter to configure this must be \ref cwmod_param
	 */
/*04*/	AT_SET_OPERATION_MODE,
	/** TODO: Escape characters */
/*05*/	AT_CONNECT_NETWORK,
	/** */
/*06*/	AT_GET_AVAILABLE_NETWORKS,
	/** */
/*07*/	AT_DISCONNECT_NETWORK,
	/** */
/*08*/	AT_SET_ACCESS_POINT,
	/** */
/*09*/	AT_GET_CONNECTED_IPS,
	/** */
/*10*/	AT_GET_STATUS,
	/** */
/*11*/	AT_START_CONNECTION,
	/** */
/*12*/	AT_SEND,
	/** */
/*13*/	AT_STOP_CONNECTION,
	/** */
/*14*/	AT_GET_IP,
	/** */
/*15*/	AT_SET_CONNECTION_TYPE,
	/** */
/*16*/	AT_SET_SERVER,
	/** */
/*17*/	AT_SET_TRANSPORT_MODE,
	/** */
/*18*/	AT_SET_CLIENT_TIMEOUT,
	/** */
/*19*/	AT_PING
};

/** Command type */
enum cmd_operation {
	AT_TEST_OP	= 0x1u,
	AT_QUERY_OP	= 0x2u,
	AT_SET_OP	= 0x4u,
	AT_EXECUTE_OP	= 0x8u
};


struct at_buff {
	uint8_t		*buff;
	uint32_t	len;
};

#include "at_params.h"



union out_param {
#if defined(PARSE_RESULT)
	/** Result for TEST_OPERATIONS */
	struct at_buff		test_result;
	/** Result for \ref AT_GET_VERSION */
	struct at_buff		version;
	/** Result for \ref AT_GET_IP */
	struct cifsr_param	local_ip;
	/** Result for \ref AT_GET_AVAILABLE_NETWORKS */
	struct cwlap_exec_param	available_networks;
	/** Result for \ref AT_GET_STATUS */
	struct cipstatus_param	status;
	/** Result form AT_SET_OPERATION_MODE */
	enum cwmod_param	wifi_mode;
	/** Result form AT_CONNECT_NETWORK */
	struct cwlap_querry_param wifi_info;
	/** Result form AT_SET_ACCESS_POINT */
	struct cwsap_param	ap_info;
	/** Result form AT_SET_CONNECTION_TYPE */
	struct cipmux_param	con_type;
	/** Result form AT_SET_TRANSPORT_MODE */
	struct cipmode_param	transp_mode;
	/** Result form AT_SET_CLIENT_TIMEOUT */
	struct cipsto_param	timeout;
#else
	struct at_buff		not_parsed;
#endif
};

union in_param {
	uint32_t		deep_sleep_time_ms;
	enum cwmod_param	wifi_mode;
	struct cwjap_param	network;
};

struct at_init_param {
	uint8_t		*read_buff;
	uint32_t	buff_size;
};

union in_out_param {
	union out_param out;
	union in_param	in;
};

#define CMD_BUFF_LEN	2000u //? Is enaught
#define RESULT_BUFF_LEN	2000u //? To think about this

/** If ok on list, move descriptor to .c file */
struct at_desc {
	/* Buffers */
	struct {
		uint8_t	app_response_buff[RESULT_BUFF_LEN];
		uint8_t	result_buff[RESULT_BUFF_LEN];
		uint8_t	cmd_buff[CMD_BUFF_LEN];
	} 			buffers;
	struct at_buff		app_response;
	struct at_buff		result;
	struct at_buff		cmd;
	/** Given by the user */
	struct at_buff		received_package;
	uint8_t			read_ch[1];
	/* States */
	/** True when received +IPD and waiting for message to be read */
	uint32_t		reading_message;
	/** 1 when result for command received, 0 when not and -1 on error */
	volatile enum {
		WAITING_RESULT,
		RESULT_AVAILABE,
		RESULT_ERROR,
		OVERFLOW_ERROR
	}			result_status;
	/** Used to check if the ok or error response arrived.*/
	uint32_t		check_idx[2];
	/* Handlers */
	struct timer_desc	*timer;
	struct uart_desc	*uart_desc;
};


// Maybe modify the return
int32_t at_init(struct at_desc **desc, struct at_init_param *param);
int32_t at_remove(struct at_desc *desc);

int32_t get_last_message(struct at_desc *desc, struct at_buff *msg);

int32_t at_run_cmd(struct at_desc *desc, enum at_cmd cmd, enum cmd_operation op,
			union in_out_param *param);

//
void	str_to_at(struct at_buff *dest, uint8_t *src);
//uint8_t	*at_to_str(struct at_buff *src);

// To think about this. 1 connection and mode 0 -> like our application
/*
void enter_sending_mode();
void send();
void exit_sending_mode();
*/
#endif
