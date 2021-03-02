//
// Created by hiep 10/4/2020
//
#include "mbtcp/modbus.h"

int modbus::Getparam(ros::NodeHandlePtr nodeHandle, info &param)
{   
	if(!nodeHandle->getParam("IP", param.ip_address) ||
	   !nodeHandle->getParam("PORT", param.port)     ||
	   !nodeHandle->getParam("ID", param.id))
    {
        ROS_ERROR("Modbus TCP: no can nodes found in yaml-file, please check configuration. Aborting...");
        return EXIT_CONFIG_ERROR;
    }else return EXIT_OK;
}

/**
 * Main Constructor of Modbus Connector Object
 * @param host IP Address of Host
 * @param port Port for the TCP Connection
 * @return     A Modbus Connector Object
 */
modbus::modbus(ros::NodeHandlePtr nodeHandle) 
{
    this->Getparam(nodeHandle,this->plcParam);
    this->HOST = this->plcParam.ip_address;
    this->PORT = (uint16_t)this->plcParam.port;
    this->_slaveid = this->plcParam.id;
    this->_msg_id = 1;
    this->_connected = false;
    this->err = false;
    this->err_no = 0;
    this->error_msg = "";
}



/**
 * Destructor of Modbus Connector Object
 */
modbus::~modbus(void) = default;


/**
 * Modbus Slave ID Setter
 * @param id  ID of the Modbus Server Slave
 */
void modbus::modbus_set_slave_id(int id) {
    this->_slaveid = id;
}



/**
 * Build up a Modbus/TCP Connection
 * @return   If A Connection Is Successfully Built
 */
bool modbus::modbus_connect() {
    if(this->HOST.empty() || this->PORT == 0) {
        ROS_ERROR_STREAM("Modbus TCP: Missing Host and Port");
        return false;
    } else {
        ROS_INFO_STREAM("Modbus TCP: Found Proper Host "<< this->HOST << " and Port " <<this->PORT);
    }

    this->_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(this->_socket == -1) {
        ROS_ERROR_STREAM("Modbus TCP: Error Opening Socket");
        return false;
    } else {
        ROS_INFO_STREAM("Modbus TCP: Socket Opened Successfully");
    }

    struct timeval timeout{};
    timeout.tv_sec  = 20;  // after 20 seconds connect() will timeout
    timeout.tv_usec = 0;
    setsockopt(_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    _server.sin_family = AF_INET;
    _server.sin_addr.s_addr = inet_addr(HOST.c_str());
    _server.sin_port = htons(PORT);

    if (connect(_socket, (struct sockaddr*)&_server, sizeof(_server)) < 0) {
        ROS_ERROR_STREAM("Modbus TCP: Host "<< this->HOST << " and Port " <<this->PORT << " Connection Error");
        return false;
    }

    ROS_INFO_STREAM("Modbus TCP: Host "<< this->HOST << " and Port " <<this->PORT << "Connected");
    _connected = true;
    return true;
}


/**
 * Close the Modbus/TCP Connection
 */
void modbus::modbus_close() const {
    close(_socket);
    ROS_INFO_STREAM("Modbus TCP: Socket Closed");
}


/**
 * Modbus Request Builder
 * @param to_send   Message Buffer to Be Sent
 * @param address   Reference Address
 * @param func      Modbus Functional Code
 */
void modbus::modbus_build_request(uint8_t *to_send, uint address, int func) const {
    to_send[0] = (uint8_t) _msg_id >> 8u;
    to_send[1] = (uint8_t) (_msg_id & 0x00FFu);
    to_send[2] = 0;
    to_send[3] = 0;
    to_send[4] = 0;
    to_send[6] = (uint8_t) _slaveid;
    to_send[7] = (uint8_t) func;
    to_send[8] = (uint8_t) (address >> 8u);
    to_send[9] = (uint8_t) (address & 0x00FFu);
}


/**
 * Write Request Builder and Sender
 * @param address   Reference Address
 * @param amount    Amount of data to be Written
 * @param func      Modbus Functional Code
 * @param value     Data to Be Written
 */
int modbus::modbus_write(int address, uint amount, int func, const uint16_t *value) {
    int status = 0;
    if(func == WRITE_COIL || func == WRITE_REG) {
        uint8_t to_send[12];
        modbus_build_request(to_send, address, func);
        to_send[5] = 6;
        to_send[10] = (uint8_t) (value[0] >> 8u);
        to_send[11] = (uint8_t) (value[0] & 0x00FFu);
        status = modbus_send(to_send, 12);
    } else if(func == WRITE_REGS){
        uint8_t to_send[13 + 2 * amount];
        modbus_build_request(to_send, address, func);
        to_send[5] = (uint8_t) (7 + 2 * amount);
        to_send[10] = (uint8_t) (amount >> 8u);
        to_send[11] = (uint8_t) (amount & 0x00FFu);
        to_send[12] = (uint8_t) (2 * amount);
        for(int i = 0; i < amount; i++) {
            to_send[13 + 2 * i] = (uint8_t) (value[i] >> 8u);
            to_send[14 + 2 * i] = (uint8_t) (value[i] & 0x00FFu);
        }
        status = modbus_send(to_send, 13 + 2 * amount);
    } else if(func == WRITE_COILS) {
        uint8_t to_send[14 + (amount -1) / 8 ];
        modbus_build_request(to_send, address, func);
        to_send[5] = (uint8_t) (7 + (amount + 7) / 8);
        to_send[10] = (uint8_t) (amount >> 8u);
        to_send[11] = (uint8_t) (amount & 0x00FFu);
        to_send[12] = (uint8_t) ((amount + 7) / 8);
        for(int i = 0; i < (amount+7)/8; i++)
            to_send[13 + i] = 0; // init needed before summing!
        for(int i = 0; i < amount; i++) {
            to_send[13 + i/8] += (uint8_t) (value[i] << (i % 8u));
        }
        status = modbus_send(to_send, 14 + (amount - 1) / 8);
    }
    return status;

}


/**
 * Read Request Builder and Sender
 * @param address   Reference Address
 * @param amount    Amount of Data to Read
 * @param func      Modbus Functional Code
 */
int modbus::modbus_read(int address, uint amount, int func){
    uint8_t to_send[12];
    modbus_build_request(to_send, address, func);
    to_send[5] = 6;
    to_send[10] = (uint8_t) (amount >> 8u);
    to_send[11] = (uint8_t) (amount & 0x00FFu);
    return modbus_send(to_send, 12);
}


/**
 * Read Holding Registers
 * MODBUS FUNCTION 0x03
 * @param address    Reference Address
 * @param amount     Amount of Registers to Read
 * @param buffer     Buffer to Store Data Read from Registers
 */
int modbus::modbus_read_holding_registers(int address, int amount, uint16_t *buffer) {
    if(_connected) {
        if(amount > 65535 || address > 65535) {
            set_bad_input();
            return EX_BAD_DATA;
        }
        modbus_read(address, amount, READ_REGS);
        uint8_t to_rec[MAX_MSG_LENGTH];
        ssize_t k = modbus_receive(to_rec);
        if (k == -1) {
            set_bad_con();
            return BAD_CON;
        }
        modbuserror_handle(to_rec, READ_REGS);
        if(err) return err_no;
        for(uint i = 0; i < amount; i++) {
            buffer[i] = ((uint16_t)to_rec[9u + 2u * i]) << 8u;
            buffer[i] += (uint16_t) to_rec[10u + 2u * i];
        }
        return 0;
    } else {
        set_bad_con();
        return BAD_CON;
    }
}


/**
 * Read Input Registers
 * MODBUS FUNCTION 0x04
 * @param address     Reference Address
 * @param amount      Amount of Registers to Read
 * @param buffer      Buffer to Store Data Read from Registers
 */
int modbus::modbus_read_input_registers(int address, int amount, uint16_t *buffer) {
    if(_connected){
        if(amount > 65535 || address > 65535) {
            set_bad_input();
            return EX_BAD_DATA;
        }
        modbus_read(address, amount, READ_INPUT_REGS);
        uint8_t to_rec[MAX_MSG_LENGTH];
        ssize_t k = modbus_receive(to_rec);
        if (k == -1) {
            set_bad_con();
            return BAD_CON;
        }
        modbuserror_handle(to_rec, READ_INPUT_REGS);
        if(err) return err_no;
        for(uint i = 0; i < amount; i++) {
            buffer[i] = ((uint16_t)to_rec[9u + 2u * i]) << 8u;
            buffer[i] += (uint16_t) to_rec[10u + 2u * i];
        }
        return 0;
    } else {
        set_bad_con();
        return BAD_CON;
    }
}


/**
 * Read Coils
 * MODBUS FUNCTION 0x01
 * @param address     Reference Address
 * @param amount      Amount of Coils to Read
 * @param buffer      Buffer to Store Data Read from Coils
 */
int modbus::modbus_read_coils(int address, int amount, bool *buffer) {
    if(_connected) {
        if(amount > 2040 || address > 65535) {
            set_bad_input();
            return EX_BAD_DATA;
        }
        modbus_read(address, amount, READ_COILS);
        uint8_t to_rec[MAX_MSG_LENGTH];
        ssize_t k = modbus_receive(to_rec);
        if (k == -1) {
            set_bad_con();
            return BAD_CON;
        }
        modbuserror_handle(to_rec, READ_COILS);
        if(err) return err_no;
        for(uint i = 0; i < amount; i++) {
            buffer[i] = (bool) ((to_rec[9u + i / 8u] >> (i % 8u)) & 1u);
        }
        return 0;
    } else {
        set_bad_con();
        return BAD_CON;
    }
}


/**
 * Read Input Bits(Discrete Data)
 * MODBUS FUNCITON 0x02
 * @param address   Reference Address
 * @param amount    Amount of Bits to Read
 * @param buffer    Buffer to store Data Read from Input Bits
 */
int modbus::modbus_read_input_bits(int address, int amount, bool* buffer) {
    if(_connected) {
        if(amount > 2040 || address > 65535) {
            set_bad_input();
            return EX_BAD_DATA;
        }
        modbus_read(address, amount, READ_INPUT_BITS);
        uint8_t to_rec[MAX_MSG_LENGTH];
        ssize_t k = modbus_receive(to_rec);
        if (k == -1) {
            set_bad_con();
            return BAD_CON;
        }
        if(err) return err_no;
        for(uint i = 0; i < amount; i++) {
            buffer[i] = (bool) ((to_rec[9u + i / 8u] >> (i % 8u)) & 1u);
        }
        modbuserror_handle(to_rec, READ_INPUT_BITS);
        return 0;
    } else {
        return BAD_CON;
    }
}


/**
 * Write Single Coils
 * MODBUS FUNCTION 0x05
 * @param address    Reference Address
 * @param to_write   Value to be Written to Coil
 */
int modbus::modbus_write_coil(int address, const bool& to_write) {
    if(_connected) {
        if(address > 65535) {
            set_bad_input();
            return EX_BAD_DATA;
        }
        int value = to_write * 0xFF00;
        modbus_write(address, 1, WRITE_COIL, (uint16_t *)&value);
        uint8_t to_rec[MAX_MSG_LENGTH];
        ssize_t k = modbus_receive(to_rec);
        if (k == -1) {
            set_bad_con();
            return BAD_CON;
        }
        modbuserror_handle(to_rec, WRITE_COIL);
        if(err) return err_no;
        return 0;
    } else {
        set_bad_con();
        return BAD_CON;
    }
}


/**
 * Write Single Register
 * FUCTION 0x06
 * @param address   Reference Address
 * @param value     Value to Be Written to Register
 */
int modbus::modbus_write_register(int address, const uint16_t& value) {
    if(_connected) {
        if(address > 65535) {
            set_bad_input();
            return EX_BAD_DATA;
        }
        modbus_write(address, 1, WRITE_REG, &value);
        uint8_t to_rec[MAX_MSG_LENGTH];
        ssize_t k = modbus_receive(to_rec);
        if (k == -1) {
            set_bad_con();
            return BAD_CON;
        }
        modbuserror_handle(to_rec, WRITE_COIL);
        if(err) return err_no;
        return 0;
    } else {
        set_bad_con();
        return BAD_CON;
    }
}


/**
 * Write Multiple Coils
 * MODBUS FUNCTION 0x0F
 * @param address  Reference Address
 * @param amount   Amount of Coils to Write
 * @param value    Values to Be Written to Coils
 */
int modbus::modbus_write_coils(int address, int amount, const bool *value) {
    if(_connected) {
        if(address > 65535 || amount > 65535) {
            set_bad_input();
            return EX_BAD_DATA;
        }
        uint16_t temp[amount];
        for(int i = 0; i < amount; i++) {
            temp[i] = (uint16_t)value[i];
        }
        modbus_write(address, amount, WRITE_COILS, temp);
        uint8_t to_rec[MAX_MSG_LENGTH];
        ssize_t k = modbus_receive(to_rec);
        if (k == -1) {
            set_bad_con();
            return BAD_CON;
        }
        modbuserror_handle(to_rec, WRITE_COILS);
        if(err) return err_no;
        return 0;
    } else {
        set_bad_con();
        return BAD_CON;
    }
}


/**
 * Write Multiple Registers
 * MODBUS FUNCION 0x10
 * @param address Reference Address
 * @param amount  Amount of Value to Write
 * @param value   Values to Be Written to the Registers
 */
int modbus::modbus_write_registers(int address, int amount, const uint16_t *value) {
    if(_connected) {
        if(address > 65535 || amount > 65535) {
            set_bad_input();
            return EX_BAD_DATA;
        }
        modbus_write(address, amount, WRITE_REGS, value);
        uint8_t to_rec[MAX_MSG_LENGTH];
        ssize_t k = modbus_receive(to_rec);
        if (k == -1) {
            set_bad_con();
            return BAD_CON;
        }
        modbuserror_handle(to_rec, WRITE_REGS);
        if(err) return err_no;
        return 0;
    } else {
        set_bad_con();
        return BAD_CON;
    }
}


/**
 * Data Sender
 * @param to_send Request to Be Sent to Server
 * @param length  Length of the Request
 * @return        Size of the request
 */
ssize_t modbus::modbus_send(uint8_t *to_send, int length) {
    _msg_id++;
    return send(_socket, to_send, (size_t)length, 0);
}


/**
 * Data Receiver
 * @param buffer Buffer to Store the Data Retrieved
 * @return       Size of Incoming Data
 */
ssize_t modbus::modbus_receive(uint8_t *buffer) const {
    return recv(_socket, (char *) buffer, 1024, 0);
}

void modbus::set_bad_con() {
    err = true;
    error_msg = "BAD CONNECTION";
}


void modbus::set_bad_input() {
    err = true;
    error_msg = "BAD FUNCTION INPUT";
}

/**
 * Error Code Handler
 * @param msg   Message Received from the Server
 * @param func  Modbus Functional Code
 */
void modbus::modbuserror_handle(const uint8_t *msg, int func) {
    if(msg[7] == func + 0x80) {
        err = true;
        switch(msg[8]){
            case EX_ILLEGAL_FUNCTION:
                error_msg = "1 Illegal Function";
                break;
            case EX_ILLEGAL_ADDRESS:
                error_msg = "2 Illegal Address";
                break;
            case EX_ILLEGAL_VALUE:
                error_msg = "3 Illegal Value";
                break;
            case EX_SERVER_FAILURE:
                error_msg = "4 Server Failure";
                break;
            case EX_ACKNOWLEDGE:
                error_msg = "5 Acknowledge";
                break;
            case EX_SERVER_BUSY:
                error_msg = "6 Server Busy";
                break;
            case EX_NEGATIVE_ACK:
                error_msg = "7 Negative Acknowledge";
                break;
            case EX_MEM_PARITY_PROB:
                error_msg = "8 Memory Parity Problem";
                break;
            case EX_GATEWAY_PROBLEMP:
                error_msg = "10 Gateway Path Unavailable";
                break;
            case EX_GATEWYA_PROBLEMF:
                error_msg = "11 Gateway Target Device Failed to Respond";
                break;
            default:
                error_msg = "UNK";
                break;
        }
    }
    err = false;
    error_msg = "NO ERR";
}