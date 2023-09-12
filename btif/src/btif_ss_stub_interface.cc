/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */
#ifdef SS_STUB_ENABLED

#define LOG_TAG "btif_ss_stub_interface"
#include "btif_ss_stub_interface.h"
#include "protobuf/proto/dm.pb.h"
#include "protobuf/proto/rfcomm.pb.h"
#include "protobuf/proto/sdp.pb.h"
#include "protobuf/proto/advertiser.pb.h"
#include "protobuf/include/proto_message_ids.h"
//#include "osi/include/log.h"
#include "btif_api.h"
#include "btif_sock_rfc.h"
#include "btif_storage.h"
#include "btif_ss_advertiser.h"
#include <utils/Log.h>
#include<vector>

using namespace std;

#define MSG_EXIT_THREAD	1
#define MSG_WRITE_STUB 2
bool outgoing = false;
uint8_t adv_inst_id = 0;

BluetoothSSStubInterface* BluetoothSSStubInterface::instance = NULL;
using namespace bluetooth::synergy::SynergyProto;
/*
using bluetooth::synergy::SynergyProto::ss_adapter_state_changed_callback;
using bluetooth::synergy::SynergyProto::ss_bt_state_t;
using bluetooth::synergy::SynergyProto::ss_rfcomm_scn_callback;
using bluetooth::synergy::SynergyProto::ss_create_socket_channel;
using bluetooth::synergy::SynergyProto::ss_bt_property_type_t;
using bluetooth::synergy::SynergyProto::ss_bt_property_t;
using bluetooth::synergy::SynergyProto::ss_discovery_state_changed_callback;
using bluetooth::synergy::SynergyProto::ss_bt_discovery_state_t;
using bluetooth::synergy::SynergyProto::ss_device_found_callback;
using bluetooth::synergy::SynergyProto::SS_BT_PROPERTY_BDADDR;
using bluetooth::synergy::SynergyProto::SS_BT_PROPERTY_BDNAME;
using bluetooth::synergy::SynergyProto::ss_adapter_properties_callback;
using bluetooth::synergy::SynergyProto::ss_sdp_search_complete_callback;
*/

uint32_t scn = 1;
std::string local_bd_addr = "22:22:24:e8:85:6e";

struct ThreadMsg
{
  ThreadMsg(int i, std::shared_ptr<void> m) { id = i; msg = m; }
  int id;
  std::shared_ptr<void> msg;
};

BluetoothSSStubInterface* BluetoothSSStubInterface::getInstance() {
  if(instance == NULL){
    ALOGI("BluetoothSSStubInterface instance is null... so creating new");
    instance = new BluetoothSSStubInterface();
  }else{
    ALOGI("BluetoothSSStubInterface instance is already created");
  }
  return instance;
}

BluetoothSSStubInterface::BluetoothSSStubInterface() {
  ALOGI("BluetoothSSStubInterface constructor");
  running_ = true;
  if (!tx_thread){
    tx_thread = std::unique_ptr<std::thread>(new std::thread(&BluetoothSSStubInterface::processTx, this));
  }
  if(!rx_thread){
    rx_thread = std::unique_ptr<std::thread>(new std::thread(&BluetoothSSStubInterface::processRx, this));
  }
}

BluetoothSSStubInterface::~BluetoothSSStubInterface() {
  ALOGI("BluetoothSSStubInterface destructor");
  exitTxThread();
  running_ = false;
}

void BluetoothSSStubInterface::processTx()
{
  ALOGI("BluetoothSSStubInterface processTx :: Started");
  while (1)
  {
    std::shared_ptr<ThreadMsg> msg;
    {
      // Wait for a message to be added to the queue
      std::unique_lock<std::mutex> lk(tx_mutex);
      while (tx_queue.empty())
        tx_cv.wait(lk);

      if (tx_queue.empty())
        continue;

      msg = tx_queue.front();
      tx_queue.pop();
    }

    switch (msg->id)
    {
      case MSG_WRITE_STUB:
        {
          ALOGI("MSG_WRITE_STUB");
          std::unique_lock<std::mutex> lk(rx_mutex);
          rx_queue.push(msg);
          rx_cv.notify_one();
          break;
        }

      case MSG_EXIT_THREAD:
        {
          return;
        }

      default:
        break;
    }
  }
}

void BluetoothSSStubInterface::postTxMsg(std::string msgStr)
{
  ALOGI("BluetoothSSStubInterface postTxMsg length is :: %d",msgStr.length());
  ALOGI("BluetoothSSStubInterface postTxMsg Msg :: %s",msgStr.c_str());
  std::shared_ptr<TxData> txData(new TxData());
  txData->msg = msgStr;
  // Create a new ThreadMsg
  std::shared_ptr<ThreadMsg> threadMsg(new ThreadMsg(MSG_WRITE_STUB, txData));
  // Add user data msg to queue and notify worker thread
  std::unique_lock<std::mutex> lk(tx_mutex);
  tx_queue.push(threadMsg);
  tx_cv.notify_one();
}

void BluetoothSSStubInterface::exitTxThread()
{
  if (!tx_thread)
    return;

  // Create a new ThreadMsg
  std::shared_ptr<ThreadMsg> threadMsg(new ThreadMsg(MSG_EXIT_THREAD, 0));

  // Put exit thread message into the queue
  {
    lock_guard<std::mutex> lock(tx_mutex);
    tx_queue.push(threadMsg);
    tx_cv.notify_one();
  }

  tx_thread->join();
  tx_thread = nullptr;

  if (!rx_thread)
    return;
  // Put exit thread message into the queue
  {
    lock_guard<std::mutex> lock(rx_mutex);
    rx_queue.push(threadMsg);
    rx_cv.notify_one();
  }

  rx_thread->join();
  rx_thread = nullptr;


}

void BluetoothSSStubInterface::processRx()
{
  ALOGI("BluetoothSSStubInterface processRx :: running_ is :: %d",running_);
  while (1)
  {
    std::shared_ptr<ThreadMsg> msg;
    {
      // Wait for a message to be added to the queue
      std::unique_lock<std::mutex> lk(rx_mutex);
      while (rx_queue.empty())
        rx_cv.wait(lk);

      if (rx_queue.empty())
        continue;

      msg = rx_queue.front();
      rx_queue.pop();
    }

    switch (msg->id)
    {
      case MSG_WRITE_STUB:
        {
          auto rxData = std::static_pointer_cast<TxData>(msg->msg);
          std::vector<uint8_t> protoVector(rxData->msg.begin(), rxData->msg.end());
          uint8_t *proto_msg = &protoVector[0];
          std::string resBufferString;
          uint16_t MSG_ID = proto_msg[0] + (((int)(proto_msg[1]))<<8);
          uint16_t length = proto_msg[2] + (((int)(proto_msg[3]))<<8);
          uint16_t proto_ec = 0;
          if (length > 0) {
            proto_ec = proto_msg[4] + (((int)(proto_msg[5]))<<8);
            char resBuffer[length];
            int j = 0;
            for(int i=MSG_PROTO_OFFSET; i< (length + MSG_PROTO_OFFSET); i++){
              resBuffer[j] = (char)proto_msg[i];
              j++;
            }
            resBufferString.assign(resBuffer, length);
          }
          //You can add another function from here to decode actual data received in TX based on msg id
          ALOGI("MSG_ID is :: %02x , Proto length: %d and Proto Encoded Value %d and Payload String: %s",MSG_ID, length, proto_ec, resBufferString.c_str());
          sendDummyCallback(MSG_ID,resBufferString);
          break;
        }

      case MSG_EXIT_THREAD:
        {
          return;
        }

      default:
        break;
    }
  }
}

void BluetoothSSStubInterface::PrintEnBytes(const char* en_char, uint8_t len) {
  ALOGD("Size: %d\n", len);
  for (uint8_t i = 0; i < len; i++) ALOGD("0x%x ", en_char[i]);
}

int BluetoothSSStubInterface::FormRxPacket(uint16_t msg_id, uint16_t proto_enc,
                                            uint16_t encode_len,
                                            std::string& encoded_bytes) {
  ALOGD("\n FormRxPacket msg_id : %d , encode_len : %d", msg_id, encode_len);
  uint8_t form_rx_msg[MAX_LENGTH_WITH_PROTO_NONE];
  char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
  form_rx_msg[0] = msg_id & 0xff;
  form_rx_msg[1] = (msg_id >> 8);
  form_rx_msg[2] = encode_len & 0xff;
  form_rx_msg[3] = (encode_len >> 8);
  form_rx_msg[4] = proto_enc & 0xff;
  form_rx_msg[5] = (proto_enc >> 8);

  memcpy(resBuffer, (char*)form_rx_msg, MAX_LENGTH_WITH_PROTO_NONE);
  std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
  msgStr.append(encoded_bytes);
  const char* encodeChars = msgStr.c_str();
  PrintEnBytes(encodeChars, encode_len);

  tBTIF_SS_Cback ss_cback;
  memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
  ss_cback.payload = (uint8_t*)malloc((MAX_LENGTH_WITH_PROTO_NONE + encode_len) * sizeof(uint8_t));  // This memory should be released from each profile after done with the processing
  if (ss_cback.payload == NULL) {
  return BT_STATUS_FAIL;
  }

  std::vector<uint8_t> protoVector(msgStr.begin(), msgStr.end());

  uint8_t* proto_msg = &protoVector[0];
  memcpy(ss_cback.payload, proto_msg,
         ((MAX_LENGTH_WITH_PROTO_NONE + encode_len) * sizeof(uint8_t)));

  btif_transfer_context(btif_advertiser_ss_callback, msg_id, (char*)&ss_cback,
                        sizeof(ss_cback), NULL);
  return BT_STATUS_SUCCESS;
}
void BluetoothSSStubInterface::sendDummyCallback(int msg_id, std::string res_buffer) {
  ALOGI("sendDummyCallback msg_id is :: %02x",msg_id);
  ALOGI("sendDummyCallback res_buffer length is :: %d",res_buffer.length());

  switch (msg_id) {
    case BT_DM_ENABLE:
    case BT_DM_DISABLE:
      {
        uint8_t adap_statecb_msg[MAX_LENGTH_WITH_PROTO_NONE];
        //adding msg_id
        uint16_t MSG_ID = BT_DM_ADAPTER_STATE_CHANGE_CB;
        adap_statecb_msg[0] = MSG_ID & 0xff;
        adap_statecb_msg[1] = (MSG_ID >> 8);

        std::string protoMsg;
        ss_adapter_state_changed_callback adapterStateChangeCb;
        if (msg_id == BT_DM_ENABLE) {
          adapterStateChangeCb.set_state(bluetooth::synergy::SynergyProto::SS_BT_STATE_ON);
        } else {
          adapterStateChangeCb.set_state(bluetooth::synergy::SynergyProto::SS_BT_STATE_OFF);
        }
        adapterStateChangeCb.SerializeToString(&protoMsg);
        ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());
        //adding length
        uint16_t length = protoMsg.length();
        adap_statecb_msg[2] = length & 0xff;
        adap_statecb_msg[3] = (length >> 8);
        //adding proto_encode
        uint16_t proto_encode = PROTO_ENC_DEC;
        adap_statecb_msg[4] = proto_encode & 0xff;
        adap_statecb_msg[5] = (proto_encode >> 8);
        char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
        memcpy(resBuffer, (char *) adap_statecb_msg, MAX_LENGTH_WITH_PROTO_NONE);
        std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
        msgStr.append(protoMsg);

        tBTIF_SS_Cback ss_cback;
        memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
        ss_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + length)*sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
        std::vector<uint8_t> protoVector(msgStr.begin(), msgStr.end());
        uint8_t *proto_msg = &protoVector[0];
        memcpy(ss_cback.payload, proto_msg, ((MAX_LENGTH_WITH_PROTO_NONE + length) * sizeof(uint8_t)) );

        btif_transfer_context(btif_dm_ss_callback, MSG_ID, (char*)&ss_cback, sizeof(ss_cback),NULL);

	if(msg_id == BT_DM_ENABLE) {
	   usleep(300000);
	   ALOGI("In case of enable, send adapter properties callback");
	   sendDummyAdapterPropCallback();
        }
        break;
      }
    case BT_DM_START_DISCOVERY:
    case BT_DM_CANCEL_DISCOVERY:
      {
       uint8_t disc_statecb_msg[MAX_LENGTH_WITH_PROTO_NONE];
        //adding msg_id
        uint16_t MSG_ID = BT_DM_DISCOVERY_STATE_CHANGE_CB;
        disc_statecb_msg[0] = MSG_ID & 0xff;
        disc_statecb_msg[1] = (MSG_ID >> 8);

        std::string protoMsg;
        ss_discovery_state_changed_callback discoveryStateChanged;;
        if (msg_id == BT_DM_START_DISCOVERY) {
          discoveryStateChanged.set_state(bluetooth::synergy::SynergyProto::SS_BT_DISCOVERY_STARTED);
        } else {
          discoveryStateChanged.set_state(bluetooth::synergy::SynergyProto::SS_BT_DISCOVERY_STOPPED);
        }
        discoveryStateChanged.SerializeToString(&protoMsg);
        ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());
        //adding length
        uint16_t length = protoMsg.length();
        disc_statecb_msg[2] = length & 0xff;
        disc_statecb_msg[3] = (length >> 8);
        //adding proto_encode
        uint16_t proto_encode = PROTO_ENC_DEC;
        disc_statecb_msg[4] = proto_encode & 0xff;
        disc_statecb_msg[5] = (proto_encode >> 8);
        char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
        memcpy(resBuffer, (char *) disc_statecb_msg, MAX_LENGTH_WITH_PROTO_NONE);
        std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
        msgStr.append(protoMsg);

        tBTIF_SS_Cback ss_cback;
        memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
        ss_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + length)*sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
        std::vector<uint8_t> protoVector(msgStr.begin(), msgStr.end());
        uint8_t *proto_msg = &protoVector[0];
        memcpy(ss_cback.payload, proto_msg, ((MAX_LENGTH_WITH_PROTO_NONE + length) * sizeof(uint8_t)) );

        btif_transfer_context(btif_dm_ss_callback, MSG_ID, (char*)&ss_cback, sizeof(ss_cback),NULL);

	if (msg_id == BT_DM_START_DISCOVERY) {
        ALOGI("%s: incase of start discovery, need to sned the dummy remote devices", __func__);
		usleep(300000);
        sendDummydeviceCallback();
	}
  //For testing incoming pairing sequence
  if (msg_id == BT_DM_CANCEL_DISCOVERY && outgoing == false)
  {
    //1. Send acl state change callback
     uint8_t acl_statecb_msg[MAX_LENGTH_WITH_PROTO_NONE];
        //adding msg_id
        uint16_t ACL_MSG_ID = BT_DM_ACL_STATE_CHANGE_CB;
        acl_statecb_msg[0] = ACL_MSG_ID & 0xff;
        acl_statecb_msg[1] = (ACL_MSG_ID >> 8);

        std::string acl_protoMsg;
        ss_acl_state_changed_callback aclStateChangedCb;
        aclStateChangedCb.set_state(bluetooth::synergy::SynergyProto::SS_BT_ACL_STATE_CONNECTED);
        aclStateChangedCb.set_status(bluetooth::synergy::SynergyProto::SS_BT_STATUS_SUCCESS);
        std::string bt_addstr_acl = "22:22:66:f2:fa:33";
        uint8_t acl_bd_addr[249];
        strlcpy((char*)acl_bd_addr, (char*)bt_addstr_acl.c_str(), sizeof(acl_bd_addr));
        aclStateChangedCb.set_remote_bd_addr(&acl_bd_addr,bt_addstr_acl.length());
        aclStateChangedCb.set_hci_reason(0);
        ALOGI("Corestack : Message %d", ACL_MSG_ID);
        aclStateChangedCb.SerializeToString(&acl_protoMsg);
        ALOGI("%s: ACL state change cb : protoMsg length is %d", __func__, acl_protoMsg.length());
        //adding length
        uint16_t length_acl = acl_protoMsg.length();
        acl_statecb_msg[2] = length_acl & 0xff;
        acl_statecb_msg[3] = (length_acl >> 8);
        //adding proto_encode
        uint16_t proto_encode_acl = PROTO_ENC_DEC;
        acl_statecb_msg[4] = proto_encode_acl & 0xff;
        acl_statecb_msg[5] = (proto_encode_acl >> 8);
        char res_Buffer[MAX_LENGTH_WITH_PROTO_NONE];
        memcpy(res_Buffer, (char *) acl_statecb_msg, MAX_LENGTH_WITH_PROTO_NONE);
	std::string msgStrAcl(res_Buffer, MAX_LENGTH_WITH_PROTO_NONE);
        msgStrAcl.append(acl_protoMsg);

        tBTIF_SS_Cback acl_ss_cback;
        memset(&acl_ss_cback, 0, sizeof(tBTIF_SS_Cback));
        acl_ss_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + length_acl)*sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
        std::vector<uint8_t> proto_Vector(msgStrAcl.begin(), msgStrAcl.end());
        uint8_t *acl_proto_msg = &proto_Vector[0];
        memcpy(acl_ss_cback.payload, acl_proto_msg, ((MAX_LENGTH_WITH_PROTO_NONE + length_acl) * sizeof(uint8_t)) );

        btif_transfer_context(btif_dm_ss_callback, ACL_MSG_ID, (char*)&acl_ss_cback, sizeof(acl_ss_cback),NULL);

	usleep(200000);
      //2. Send Remote device properties callback
        uint8_t remote_device_properties_callback_msg[MAX_LENGTH_WITH_PROTO_NONE];
        uint16_t Msg_id = BT_DM_REMOTE_DEVICE_PROPERTIES_CB;
        remote_device_properties_callback_msg[0] = Msg_id & 0xFF;
        remote_device_properties_callback_msg[1] = Msg_id >> 8;

        std::string protoMsgCb;
        ss_remote_device_properties_callback remoteDevicePropertiesCb;
        remoteDevicePropertiesCb.set_status((ss_bt_status_t)SS_BT_STATUS_SUCCESS);
        remoteDevicePropertiesCb.set_bd_addr("22:22:66:f2:fa:33", 17);
        remoteDevicePropertiesCb.set_num_properties(3);
        ss_bt_property_t* property_name = remoteDevicePropertiesCb.add_properties();
        property_name->set_type((ss_bt_property_type_t)SS_BT_PROPERTY_BDNAME);
        property_name->set_len(9);
        property_name->set_val("TEST_NAME", 9);
        ss_bt_property_t* property_class = remoteDevicePropertiesCb.add_properties();
        property_class->set_type((ss_bt_property_type_t)SS_BT_PROPERTY_CLASS_OF_DEVICE);
        property_class->set_len(1);
        property_class->set_val("2");
        ss_bt_property_t* property_type = remoteDevicePropertiesCb.add_properties();
        property_type->set_type((ss_bt_property_type_t)SS_BT_PROPERTY_TYPE_OF_DEVICE);
        property_type->set_len(1);
        property_type->set_val("3");

        remoteDevicePropertiesCb.SerializeToString(&protoMsgCb);
        ALOGI("%s: Remote Device prop cb: protoMsg length is %d", __func__, protoMsgCb.length());
        uint16_t proto_length = protoMsgCb.length();
        remote_device_properties_callback_msg[2] = proto_length & 0xFF;
        remote_device_properties_callback_msg[3] = proto_length >> 8;

        //uint16_t proto_encode = PROTO_ENC_DEC;
        remote_device_properties_callback_msg[4] = proto_encode & 0xFF;
        remote_device_properties_callback_msg[5] = proto_encode >> 8;

        std::string msgStrCb((char*)remote_device_properties_callback_msg, MAX_LENGTH_WITH_PROTO_NONE);
        msgStrCb.append(protoMsgCb);
        //tBTIF_SS_Cback ss_cback;
        memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
        ss_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + proto_length)*sizeof(uint8_t));
        std::vector<uint8_t> protoVectorMsg(msgStrCb.begin(), msgStrCb.end());
        uint8_t *proto_msg_ptr = &protoVectorMsg[0];
        memcpy(ss_cback.payload, proto_msg_ptr, ((MAX_LENGTH_WITH_PROTO_NONE + proto_length) * sizeof(uint8_t)));

        btif_transfer_context(btif_dm_ss_callback, Msg_id, (char*)&ss_cback, sizeof(ss_cback), NULL);

	usleep(200000);
//3. Send bond state change callback
        uint8_t bond_statecb_msg[MAX_LENGTH_WITH_PROTO_NONE];
        //adding msg_id
        uint16_t MSG_ID = BT_DM_BOND_STATE_CHANGE_CB;
        bond_statecb_msg[0] = MSG_ID & 0xff;
        bond_statecb_msg[1] = (MSG_ID >> 8);

        std::string protoMsg;
        ss_bond_state_changed_callback bondStateChangedCb;
        bondStateChangedCb.set_state(bluetooth::synergy::SynergyProto::SS_BT_BOND_STATE_BONDING);
        bondStateChangedCb.set_status(bluetooth::synergy::SynergyProto::SS_BT_STATUS_SUCCESS);
        std::string bt_addstr = "22:22:66:f2:fa:33";
        uint8_t bd_addr[249];
        strlcpy((char*)bd_addr, (char*)bt_addstr.c_str(), sizeof(bd_addr));
        bondStateChangedCb.set_remote_bd_addr(&bd_addr,bt_addstr.length());
        bondStateChangedCb.set_fail_reason(0);

        bondStateChangedCb.SerializeToString(&protoMsg);
        ALOGI("%s: CREATE_BOND: protoMsg length is %d", __func__, protoMsg.length());
        //adding length
        uint16_t length = protoMsg.length();
        bond_statecb_msg[2] = length & 0xff;
        bond_statecb_msg[3] = (length >> 8);
        //adding proto_encode
        uint16_t proto_encode = PROTO_ENC_DEC;
        bond_statecb_msg[4] = proto_encode & 0xff;
        bond_statecb_msg[5] = (proto_encode >> 8);
        char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
        memcpy(resBuffer, (char *) bond_statecb_msg, MAX_LENGTH_WITH_PROTO_NONE);
        std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
        msgStr.append(protoMsg);

        tBTIF_SS_Cback ss_cback;
        memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
        ss_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + length)*sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
        std::vector<uint8_t> protoVector(msgStr.begin(), msgStr.end());
        uint8_t *proto_msg = &protoVector[0];
        memcpy(ss_cback.payload, proto_msg, ((MAX_LENGTH_WITH_PROTO_NONE + length) * sizeof(uint8_t)) );

        btif_transfer_context(btif_dm_ss_callback, MSG_ID, (char*)&ss_cback, sizeof(ss_cback),NULL);

        usleep(200000);

      //4. Send SSP request callback
        uint8_t ssp_request_callback_msg[MAX_LENGTH_WITH_PROTO_NONE];
        uint16_t message_id = BT_DM_SSP_REQUEST_CB;
        ssp_request_callback_msg[0] = message_id & 0xFF;
        ssp_request_callback_msg[1] = message_id >> 8;

        std::string sspProtoMsg;
        ss_ssp_request_callback sspRequestCallback;
        sspRequestCallback.set_remote_bd_addr("22:22:66:f2:fa:33", 17);
        ss_bt_bdname_t* remote_name = sspRequestCallback.mutable_bdname();
        remote_name->set_name("TEST_NAME", 9);
        sspRequestCallback.set_cod(0x1F);
        sspRequestCallback.set_pairing_variant((ss_bt_ssp_variant_t)SS_BT_SSP_VARIANT_CONSENT);
        sspRequestCallback.set_pass_key(590865);
        sspRequestCallback.SerializeToString(&sspProtoMsg);
        ALOGI("%s: SSP callback protoMsg length is %d", __func__, sspProtoMsg.length());

        uint16_t ssp_length = sspProtoMsg.length();
        ssp_request_callback_msg[2] = ssp_length & 0xFF;
        ssp_request_callback_msg[3] = ssp_length >> 8;

        uint16_t proto_encode_ssp = PROTO_ENC_DEC;
        ssp_request_callback_msg[4] = proto_encode_ssp & 0xFF;
        ssp_request_callback_msg[5] = proto_encode_ssp >> 8;

        std::string sspMsgStr((char*)ssp_request_callback_msg, MAX_LENGTH_WITH_PROTO_NONE);
        sspMsgStr.append(sspProtoMsg);
        tBTIF_SS_Cback ss_ssp_cback;
        memset(&ss_ssp_cback, 0, sizeof(tBTIF_SS_Cback));
        ss_ssp_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + ssp_length)*sizeof(uint8_t));
        std::vector<uint8_t> sspProtoVector(sspMsgStr.begin(), sspMsgStr.end());
        uint8_t *ssp_proto_msg = &sspProtoVector[0];
        memcpy(ss_ssp_cback.payload, ssp_proto_msg, ((MAX_LENGTH_WITH_PROTO_NONE + ssp_length) * sizeof(uint8_t)));

        btif_transfer_context(btif_dm_ss_callback, message_id, (char*)&ss_ssp_cback, sizeof(ss_ssp_cback), NULL);
  }
        break;
      }
    case BT_RFCOMM_CREATE_SOCKET:
      {
        //std::string proto_msg_only = res_buffer.substr(6, res_buffer.length() - 6);
        ss_create_socket_channel createSocketCh;
        bool bOk = createSocketCh.ParseFromString(res_buffer);
        ALOGI("sendDummyCallback bOk is :: %d",bOk);
        ALOGI("sendDummyCallback createSocketCh.has_sock_fd() is :: %d",createSocketCh.has_sock_fd());
        ALOGI("sendDummyCallback createSocketCh.sock_fd() is :: %d",createSocketCh.sock_fd());
        ALOGI("sendDummyCallback createSocketCh.flags() is :: %d",createSocketCh.flags());
        uint8_t rfcomm_scn_cb_msg[MAX_LENGTH_WITH_PROTO_NONE];
        //adding msg_id
        uint16_t MSG_ID = BT_RFCOMM_SCN_CB;
        rfcomm_scn_cb_msg[0] = MSG_ID & 0xff;
        rfcomm_scn_cb_msg[1] = (MSG_ID >> 8);
        std::string protoMsg;
        ss_rfcomm_scn_callback rfcommScnCallback;
        rfcommScnCallback.set_sock_fd(createSocketCh.sock_fd());
        uint32_t scn_to_send = scn++;
        rfcommScnCallback.set_scn(scn_to_send);
        rfcommScnCallback.SerializeToString(&protoMsg);
        ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());

        //adding length
        uint16_t length = protoMsg.length();
        rfcomm_scn_cb_msg[2] = length & 0xff;
        rfcomm_scn_cb_msg[3] = (length >> 8);
        //adding proto_encode
        uint16_t proto_encode = PROTO_ENC_DEC;
        rfcomm_scn_cb_msg[4] = proto_encode & 0xff;
        rfcomm_scn_cb_msg[5] = (proto_encode >> 8);
        char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
        memcpy(resBuffer, (char *) rfcomm_scn_cb_msg, MAX_LENGTH_WITH_PROTO_NONE);
        std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
        msgStr.append(protoMsg);

        tBTIF_SS_Cback ss_cback;
        memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
        ss_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + length)*sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
        std::vector<uint8_t> protoVector(msgStr.begin(), msgStr.end());
        uint8_t *proto_msg = &protoVector[0];
        memcpy(ss_cback.payload, proto_msg, ((MAX_LENGTH_WITH_PROTO_NONE + length) * sizeof(uint8_t)) );

        btif_transfer_context(btif_rfcomm_ss_callback, MSG_ID, (char*)&ss_cback, sizeof(ss_cback),NULL);
		    ALOGI("%s: before sleep ",__func__);
		    usleep(100000);//sleep
        ALOGI("%s: after sleep ",__func__);

        uint8_t rfcomm_srv_open_cb_msg[MAX_LENGTH_WITH_PROTO_NONE];
        MSG_ID = BT_RFCOMM_SRV_OPEN_CB;
        rfcomm_srv_open_cb_msg[0] = MSG_ID & 0xff;
        rfcomm_srv_open_cb_msg[1] = (MSG_ID >> 8);

        ss_rfcomm_srv_open_callback rfcommSrvOpCb;
        rfcommSrvOpCb.set_sock_fd(createSocketCh.sock_fd());
        rfcommSrvOpCb.set_channel(scn_to_send);
        rfcommSrvOpCb.set_tx_mtu(990);
        rfcommSrvOpCb.set_status(1);
        rfcommSrvOpCb.set_addr(local_bd_addr.c_str());
        rfcommSrvOpCb.SerializeToString(&protoMsg);
        ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());

        length = protoMsg.length();
        rfcomm_srv_open_cb_msg[2] = length & 0xff;
        rfcomm_srv_open_cb_msg[3] = (length >> 8);
        //adding proto_encode
        proto_encode = PROTO_ENC_DEC;
        rfcomm_srv_open_cb_msg[4] = proto_encode & 0xff;
        rfcomm_srv_open_cb_msg[5] = (proto_encode >> 8);
        char resBuffer2[MAX_LENGTH_WITH_PROTO_NONE];
        memcpy(resBuffer2, (char *) rfcomm_srv_open_cb_msg, MAX_LENGTH_WITH_PROTO_NONE);
        std::string msgStr2(resBuffer2, MAX_LENGTH_WITH_PROTO_NONE);
        msgStr2.append(protoMsg);

        memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
        ss_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + length)*sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
        std::vector<uint8_t> protoVector2(msgStr2.begin(), msgStr2.end());
        uint8_t *proto_msg2 = &protoVector2[0];
        memcpy(ss_cback.payload, proto_msg2, ((MAX_LENGTH_WITH_PROTO_NONE + length) * sizeof(uint8_t)) );

        btif_transfer_context(btif_rfcomm_ss_callback, MSG_ID, (char*)&ss_cback, sizeof(ss_cback),NULL);

      }
      break;
	  case BT_RFCOMM_CONNECT_SOCKET:
      {
        //std::string proto_msg_only = res_buffer.substr(6, res_buffer.length() - 6);
        ss_connect_socket connectSocketCh;
        connectSocketCh.ParseFromString(res_buffer);
        std::string bd_address = connectSocketCh.bd_addr();
        std::string uuid = connectSocketCh.service_uuid();
        ALOGI("sendDummyCallback connectSocketCh.sock_fd() is :: %d and addr: %s, service_uuid: %s, channel: %d and flags: %d",connectSocketCh.sock_fd(), bd_address.c_str(), uuid.c_str(), connectSocketCh.channel(), connectSocketCh.flags());
        uint8_t rfcomm_scn_cb_msg[MAX_LENGTH_WITH_PROTO_NONE];
        //adding msg_id
        uint16_t MSG_ID = BT_RFCOMM_SCN_CB;
        rfcomm_scn_cb_msg[0] = MSG_ID & 0xff;
        rfcomm_scn_cb_msg[1] = (MSG_ID >> 8);
        std::string protoMsg;
        ss_rfcomm_scn_callback rfcommScnCallback;
        rfcommScnCallback.set_sock_fd(connectSocketCh.sock_fd());
        uint32_t scn_to_send;
        if ((int)connectSocketCh.channel() > 0) {
          scn_to_send = connectSocketCh.channel();
        } else {
          scn_to_send = scn++;
        }
        rfcommScnCallback.set_scn(scn_to_send);

        rfcommScnCallback.SerializeToString(&protoMsg);
        ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());

        //adding length
        uint16_t length = protoMsg.length();
        rfcomm_scn_cb_msg[2] = length & 0xff;
        rfcomm_scn_cb_msg[3] = (length >> 8);
        //adding proto_encode
        uint16_t proto_encode = PROTO_ENC_DEC;
        rfcomm_scn_cb_msg[4] = proto_encode & 0xff;
        rfcomm_scn_cb_msg[5] = (proto_encode >> 8);
        char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
        memcpy(resBuffer, (char *) rfcomm_scn_cb_msg, MAX_LENGTH_WITH_PROTO_NONE);
        std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
        msgStr.append(protoMsg);

        tBTIF_SS_Cback ss_cback;
        memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
        ss_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + length)*sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
        std::vector<uint8_t> protoVector(msgStr.begin(), msgStr.end());
        uint8_t *proto_msg = &protoVector[0];
        memcpy(ss_cback.payload, proto_msg, ((MAX_LENGTH_WITH_PROTO_NONE + length) * sizeof(uint8_t)) );

        btif_transfer_context(btif_rfcomm_ss_callback, MSG_ID, (char*)&ss_cback, sizeof(ss_cback),NULL);
		    ALOGI("%s: before sleep ",__func__);
		    usleep(100000);//sleep
        ALOGI("%s: after sleep ",__func__);

        uint8_t rfcomm_cli_con_cb_msg[MAX_LENGTH_WITH_PROTO_NONE];
        MSG_ID = BT_RFCOMM_CLIENT_CONNECT_CB;
        rfcomm_cli_con_cb_msg[0] = MSG_ID & 0xff;
        rfcomm_cli_con_cb_msg[1] = (MSG_ID >> 8);

        ss_rfcomm_cli_connect_callback rfcommCliConCb;
        rfcommCliConCb.set_sock_fd(connectSocketCh.sock_fd());
        rfcommCliConCb.set_channel(scn_to_send);
        rfcommCliConCb.set_tx_mtu(990);
        rfcommCliConCb.set_status(1);
        rfcommCliConCb.set_addr(local_bd_addr.c_str());
        rfcommCliConCb.SerializeToString(&protoMsg);
        ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());

        length = protoMsg.length();
        rfcomm_cli_con_cb_msg[2] = length & 0xff;
        rfcomm_cli_con_cb_msg[3] = (length >> 8);
        //adding proto_encode
        proto_encode = PROTO_ENC_DEC;
        rfcomm_cli_con_cb_msg[4] = proto_encode & 0xff;
        rfcomm_cli_con_cb_msg[5] = (proto_encode >> 8);
        char resBuffer2[MAX_LENGTH_WITH_PROTO_NONE];
        memcpy(resBuffer2, (char *) rfcomm_cli_con_cb_msg, MAX_LENGTH_WITH_PROTO_NONE);
        std::string msgStr2(resBuffer2, MAX_LENGTH_WITH_PROTO_NONE);
        msgStr2.append(protoMsg);

        memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
        ss_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + length)*sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
        std::vector<uint8_t> protoVector2(msgStr2.begin(), msgStr2.end());
        uint8_t *proto_msg2 = &protoVector2[0];
        memcpy(ss_cback.payload, proto_msg2, ((MAX_LENGTH_WITH_PROTO_NONE + length) * sizeof(uint8_t)) );

        btif_transfer_context(btif_rfcomm_ss_callback, MSG_ID, (char*)&ss_cback, sizeof(ss_cback),NULL);

      }
      break;
    case BT_RFCOMM_WRITE_SOCKET_DATA:
      {
        ss_write_rfcomm_data writeData;
        writeData.ParseFromString(res_buffer);
        ALOGI("%s: SockFD: %d", __func__, writeData.sock_fd());
        ALOGI("%s: Channel: %d", __func__, writeData.channel());
        ALOGI("%s: data length is :: %d",__func__,writeData.data_len());
        std::string data_string = writeData.data();
        ALOGI("%s: Length: %d and Data %s", __func__, data_string.length(),data_string.c_str());

        uint8_t rfcomm_data_cb_msg[MAX_LENGTH_WITH_PROTO_NONE];
        //adding msg_id
        uint16_t MSG_ID = BT_RFCOMM_SOCKET_DATA_CB;
        rfcomm_data_cb_msg[0] = MSG_ID & 0xff;
        rfcomm_data_cb_msg[1] = (MSG_ID >> 8);
        std::string protoMsg;
        ss_rfcomm_data_callback rfcommDataCb;
        rfcommDataCb.set_sock_fd(writeData.sock_fd());
        rfcommDataCb.set_channel(writeData.channel());
        rfcommDataCb.set_data_len(data_string.length());
        rfcommDataCb.set_data(data_string.c_str());
        rfcommDataCb.SerializeToString(&protoMsg);
        ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());

        //adding length
        uint16_t length = protoMsg.length();
        rfcomm_data_cb_msg[2] = length & 0xff;
        rfcomm_data_cb_msg[3] = (length >> 8);
        //adding proto_encode
        uint16_t proto_encode = PROTO_RAW_BYTES;
        rfcomm_data_cb_msg[4] = proto_encode & 0xff;
        rfcomm_data_cb_msg[5] = (proto_encode >> 8);
        char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
        memcpy(resBuffer, (char *) rfcomm_data_cb_msg, MAX_LENGTH_WITH_PROTO_NONE);
        std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
        msgStr.append(protoMsg);

        tBTIF_SS_Cback ss_cback;
        memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
        ss_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + length)*sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
        std::vector<uint8_t> protoVector(msgStr.begin(), msgStr.end());
        uint8_t *proto_msg = &protoVector[0];
        memcpy(ss_cback.payload, proto_msg, ((MAX_LENGTH_WITH_PROTO_NONE + length) * sizeof(uint8_t)) );

        btif_transfer_context(btif_rfcomm_ss_callback, MSG_ID, (char*)&ss_cback, sizeof(ss_cback),NULL);
      }
      break;
  
      case BT_DM_CREATE_BOND:
      case BT_DM_CREATE_BOND_OOB:
      {
        uint8_t bond_statecb_msg[MAX_LENGTH_WITH_PROTO_NONE];
        //adding msg_id
        uint16_t MSG_ID = BT_DM_BOND_STATE_CHANGE_CB;
        bond_statecb_msg[0] = MSG_ID & 0xff;
        bond_statecb_msg[1] = (MSG_ID >> 8);

        std::string protoMsg;
        ss_bond_state_changed_callback bondStateChangedCb;
        bondStateChangedCb.set_state(bluetooth::synergy::SynergyProto::SS_BT_BOND_STATE_BONDING);
        bondStateChangedCb.set_status(bluetooth::synergy::SynergyProto::SS_BT_STATUS_SUCCESS);
        std::string bt_addstr = "22:22:44:d4:79:76";
        uint8_t bd_addr[249];
        strlcpy((char*)bd_addr, (char*)bt_addstr.c_str(), bt_addstr.length()+1);
        bondStateChangedCb.set_remote_bd_addr(&bd_addr,sizeof(bd_addr));
        bondStateChangedCb.set_fail_reason(0);

        bondStateChangedCb.SerializeToString(&protoMsg);
        ALOGI("%s: CREATE_BOND: protoMsg length is %d", __func__, protoMsg.length());
        //adding length
        uint16_t length = protoMsg.length();
        bond_statecb_msg[2] = length & 0xff;
        bond_statecb_msg[3] = (length >> 8);
        //adding proto_encode
        uint16_t proto_encode = PROTO_ENC_DEC;
        bond_statecb_msg[4] = proto_encode & 0xff;
        bond_statecb_msg[5] = (proto_encode >> 8);
        char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
        memcpy(resBuffer, (char *) bond_statecb_msg, MAX_LENGTH_WITH_PROTO_NONE);
        std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
        msgStr.append(protoMsg);

        tBTIF_SS_Cback ss_cback;
        memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
        ss_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + length)*sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
        std::vector<uint8_t> protoVector(msgStr.begin(), msgStr.end());
        uint8_t *proto_msg = &protoVector[0];
        memcpy(ss_cback.payload, proto_msg, ((MAX_LENGTH_WITH_PROTO_NONE + length) * sizeof(uint8_t)));

        btif_transfer_context(btif_dm_ss_callback, MSG_ID, (char*)&ss_cback, sizeof(ss_cback),NULL);

        usleep(200000);

        uint8_t acl_statecb_msg[MAX_LENGTH_WITH_PROTO_NONE];
        //adding msg_id
        uint16_t ACL_MSG_ID = BT_DM_ACL_STATE_CHANGE_CB;
        acl_statecb_msg[0] = ACL_MSG_ID & 0xff;
        acl_statecb_msg[1] = (ACL_MSG_ID >> 8);

        std::string acl_protoMsg;
        ss_acl_state_changed_callback aclStateChangedCb;
        aclStateChangedCb.set_state(bluetooth::synergy::SynergyProto::SS_BT_ACL_STATE_CONNECTED);
        aclStateChangedCb.set_status(bluetooth::synergy::SynergyProto::SS_BT_STATUS_SUCCESS);
        std::string bt_addstr_acl = "22:22:44:d4:79:76";
        uint8_t acl_bd_addr[249];
        strlcpy((char*)bd_addr, (char*)bt_addstr_acl.c_str(), bt_addstr_acl.length()+1);
        aclStateChangedCb.set_remote_bd_addr(&acl_bd_addr, bt_addstr_acl.length());
        aclStateChangedCb.set_hci_reason(0);
        ALOGI("CREATE_BOND: Message ID: %d", ACL_MSG_ID);
        aclStateChangedCb.SerializeToString(&acl_protoMsg);
        ALOGI("%s: CREATE_BOND: ACL state change protoMsg length is %d", __func__, protoMsg.length());
        //adding length
        uint16_t length_acl = acl_protoMsg.length();
        acl_statecb_msg[2] = length_acl & 0xff;
        acl_statecb_msg[3] = (length_acl >> 8);
        //adding proto_encode
        uint16_t proto_encode_acl = PROTO_ENC_DEC;
        acl_statecb_msg[4] = proto_encode_acl & 0xff;
        acl_statecb_msg[5] = (proto_encode_acl >> 8);
	char res_Buffer[MAX_LENGTH_WITH_PROTO_NONE];
        memcpy(res_Buffer, (char *) acl_statecb_msg, MAX_LENGTH_WITH_PROTO_NONE);
	std::string msgStrAcl(res_Buffer, MAX_LENGTH_WITH_PROTO_NONE);
        msgStrAcl.append(acl_protoMsg);

	tBTIF_SS_Cback acl_ss_cback;
        memset(&acl_ss_cback, 0, sizeof(tBTIF_SS_Cback));
        acl_ss_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + length_acl)*sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
        std::vector<uint8_t> proto_Vector(msgStrAcl.begin(), msgStrAcl.end());
	uint8_t *acl_proto_msg = &proto_Vector[0];
        memcpy(acl_ss_cback.payload, acl_proto_msg, ((MAX_LENGTH_WITH_PROTO_NONE + length_acl) * sizeof(uint8_t)));
        btif_transfer_context(btif_dm_ss_callback, ACL_MSG_ID, (char*)&acl_ss_cback, sizeof(acl_ss_cback),NULL);

        usleep(200000);
      //3. Send Remote device properties callback
        uint8_t remote_device_properties_callback_msg[MAX_LENGTH_WITH_PROTO_NONE];
        uint16_t Msg_id = BT_DM_REMOTE_DEVICE_PROPERTIES_CB;
        remote_device_properties_callback_msg[0] = Msg_id & 0xFF;
        remote_device_properties_callback_msg[1] = Msg_id >> 8;

        std::string protoMsgCb;
        ss_remote_device_properties_callback remoteDevicePropertiesCb;
        remoteDevicePropertiesCb.set_status((ss_bt_status_t)SS_BT_STATUS_SUCCESS);
        remoteDevicePropertiesCb.set_bd_addr("22:22:44:d4:79:76", 17);
        remoteDevicePropertiesCb.set_num_properties(3);
        ss_bt_property_t* property_name = remoteDevicePropertiesCb.add_properties();
        property_name->set_type((ss_bt_property_type_t)SS_BT_PROPERTY_BDNAME);
        property_name->set_len(9);
        property_name->set_val("TEST_NAME", 9);
        ss_bt_property_t* property_class = remoteDevicePropertiesCb.add_properties();
        property_class->set_type((ss_bt_property_type_t)SS_BT_PROPERTY_CLASS_OF_DEVICE);
        property_class->set_len(1);
        property_class->set_val("2");
        ss_bt_property_t* property_type = remoteDevicePropertiesCb.add_properties();
        property_type->set_type((ss_bt_property_type_t)SS_BT_PROPERTY_TYPE_OF_DEVICE);
        property_type->set_len(1);
        property_type->set_val("3");

        remoteDevicePropertiesCb.SerializeToString(&protoMsgCb);
        ALOGI("%s: Remote Device prop cb: protoMsg length is %d", __func__, protoMsgCb.length());
        uint16_t proto_length = protoMsgCb.length();
        remote_device_properties_callback_msg[2] = proto_length & 0xFF;
        remote_device_properties_callback_msg[3] = proto_length >> 8;

        //uint16_t proto_encode = PROTO_ENC_DEC;
        remote_device_properties_callback_msg[4] = proto_encode & 0xFF;
        remote_device_properties_callback_msg[5] = proto_encode >> 8;

        std::string msgStrCb((char*)remote_device_properties_callback_msg, MAX_LENGTH_WITH_PROTO_NONE);
        msgStrCb.append(protoMsgCb);
        //tBTIF_SS_Cback ss_cback;
        memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
        ss_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + proto_length)*sizeof(uint8_t));
        std::vector<uint8_t> protoVectorMsg(msgStrCb.begin(), msgStrCb.end());
        uint8_t *proto_msg_ptr = &protoVectorMsg[0];
        memcpy(ss_cback.payload, proto_msg_ptr, ((MAX_LENGTH_WITH_PROTO_NONE + proto_length) * sizeof(uint8_t)));

        btif_transfer_context(btif_dm_ss_callback, Msg_id, (char*)&ss_cback, sizeof(ss_cback), NULL);

        usleep(200000);

	//4. Send SSP request callback
        uint8_t ssp_request_callback_msg[MAX_LENGTH_WITH_PROTO_NONE];
        uint16_t message_id = BT_DM_SSP_REQUEST_CB;
        ssp_request_callback_msg[0] = message_id & 0xFF;
        ssp_request_callback_msg[1] = message_id >> 8;

        std::string sspProtoMsg;
        ss_ssp_request_callback sspRequestCallback;
        sspRequestCallback.set_remote_bd_addr("22:22:44:d4:79:76", 17);
        ss_bt_bdname_t* remote_name = sspRequestCallback.mutable_bdname();
        remote_name->set_name("TEST_NAME", 9);
        sspRequestCallback.set_cod(0x1F);
        sspRequestCallback.set_pairing_variant((ss_bt_ssp_variant_t)SS_BT_SSP_VARIANT_CONSENT);
        sspRequestCallback.set_pass_key(590865);
        sspRequestCallback.SerializeToString(&sspProtoMsg);
        ALOGI("%s: SSP callback protoMsg length is %d", __func__, sspProtoMsg.length());

        uint16_t ssp_length = sspProtoMsg.length();
        ssp_request_callback_msg[2] = ssp_length & 0xFF;
        ssp_request_callback_msg[3] = ssp_length >> 8;

        uint16_t proto_encode_ssp = PROTO_ENC_DEC;
        ssp_request_callback_msg[4] = proto_encode_ssp & 0xFF;
        ssp_request_callback_msg[5] = proto_encode_ssp >> 8;

        std::string sspMsgStr((char*)ssp_request_callback_msg, MAX_LENGTH_WITH_PROTO_NONE);
        sspMsgStr.append(sspProtoMsg);
        tBTIF_SS_Cback ss_ssp_cback;
        memset(&ss_ssp_cback, 0, sizeof(tBTIF_SS_Cback));
        ss_ssp_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + ssp_length)*sizeof(uint8_t));
        std::vector<uint8_t> sspProtoVector(sspMsgStr.begin(), sspMsgStr.end());
        uint8_t *ssp_proto_msg = &sspProtoVector[0];
        memcpy(ss_ssp_cback.payload, ssp_proto_msg, ((MAX_LENGTH_WITH_PROTO_NONE + ssp_length) * sizeof(uint8_t)));

        btif_transfer_context(btif_dm_ss_callback, message_id, (char*)&ss_ssp_cback, sizeof(ss_ssp_cback), NULL);

        break;
      }
      case BT_DM_SSP_REPLY:
      {
        uint8_t bond_statecb_msg[MAX_LENGTH_WITH_PROTO_NONE];
        //adding msg_id
        uint16_t MSG_ID = BT_DM_BOND_STATE_CHANGE_CB;
        bond_statecb_msg[0] = MSG_ID & 0xff;
        bond_statecb_msg[1] = (MSG_ID >> 8);
        ALOGI("BT_DM_SSP_REPLY: Message %d", MSG_ID);
        std::string protoMsg;
        ss_bond_state_changed_callback bondStateChangedCb;
        bondStateChangedCb.set_state(bluetooth::synergy::SynergyProto::SS_BT_BOND_STATE_BONDED);
        bondStateChangedCb.set_status(bluetooth::synergy::SynergyProto::SS_BT_STATUS_SUCCESS);
        std::string bt_addstr = "22:22:44:d4:79:76";
        uint8_t bd_addr[249];
        strlcpy((char*)bd_addr, (char*)bt_addstr.c_str(), bt_addstr.length()+1);
        bondStateChangedCb.set_remote_bd_addr(&bd_addr,sizeof(bd_addr));
        bondStateChangedCb.set_fail_reason(0);

        bondStateChangedCb.SerializeToString(&protoMsg);
        ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());
        //adding length
        uint16_t length = protoMsg.length();
        bond_statecb_msg[2] = length & 0xff;
        bond_statecb_msg[3] = (length >> 8);
        //adding proto_encode
        uint16_t proto_encode = PROTO_ENC_DEC;
        bond_statecb_msg[4] = proto_encode & 0xff;
        bond_statecb_msg[5] = (proto_encode >> 8);
        char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
        memcpy(resBuffer, (char *) bond_statecb_msg, MAX_LENGTH_WITH_PROTO_NONE);
        std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
        msgStr.append(protoMsg);

        tBTIF_SS_Cback ss_cback;
        memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
        ss_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + length)*sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
        std::vector<uint8_t> protoVector(msgStr.begin(), msgStr.end());
        uint8_t *proto_msg = &protoVector[0];
        memcpy(ss_cback.payload, proto_msg, ((MAX_LENGTH_WITH_PROTO_NONE + length) * sizeof(uint8_t)));

        btif_transfer_context(btif_dm_ss_callback, MSG_ID, (char*)&ss_cback, sizeof(ss_cback),NULL);
        break;
      }
      case BT_LE_ADV_START_ADV_SET: {
        ALOGD("\n In BT_LE_ADV_START_ADV_SET case ");
        ss_ble_start_advertising_set startAdvSet;
        startAdvSet.ParseFromString(res_buffer);
        int32_t reg_id = -1;
        int32_t tx_power = 0;
        std::vector<uint8_t> data;
        if (startAdvSet.has_regid()) {
          reg_id = startAdvSet.regid();
          ALOGD("\n reg_id: %d ", reg_id);
        }
        if (startAdvSet.has_parameters()) {
          ss_advertising_parameters adv_params = startAdvSet.parameters();
          tx_power = adv_params.txpower();
          ALOGD("\n advertisingeventproperties : %d ", adv_params.advertisingeventproperties());
          ALOGD("\n mininterval : %d ", adv_params.mininterval());
          ALOGD("\n maxinterval : %d ", adv_params.maxinterval());
          ALOGD("\n channelmap : %d ", adv_params.channelmap());
          ALOGD("\n txpower : %d ", tx_power);
          ALOGD("\n primaryadvertisingphy : %d ", adv_params.primaryadvertisingphy());
          ALOGD("\n secondaryadvertisingphy : %d ", adv_params.secondaryadvertisingphy());
          ALOGD("\n scanrequestnotificationenable : %d ", adv_params.scanrequestnotificationenable());
        }
        if (startAdvSet.has_periodicparameters()) {
          ss_periodic_advertising_parameters perioadv_params = startAdvSet.periodicparameters();
          ALOGD("\n periodicadvertisingproperties : %d ", perioadv_params.periodicadvertisingproperties());
          ALOGD("\n mininterval : %d ", perioadv_params.mininterval());
          ALOGD("\n maxinterval : %d ", perioadv_params.maxinterval());
          ALOGD("\n enable : %d ", perioadv_params.enable());
        }
        if (startAdvSet.has_duration()) {
          ALOGD("\n duration: %d ", startAdvSet.duration());
        }
        if (startAdvSet.has_maxextadvevents()) {
          ALOGD("\n maxextadvevents: %d ", startAdvSet.maxextadvevents());
        }
        if (startAdvSet.advertisedata_size() != 0) {
          for (uint16_t i = 0; i < startAdvSet.advertisedata_size(); i++)
            data.push_back(startAdvSet.advertisedata(i));
          for (uint16_t i = 0; i < data.size(); i++)
            ALOGD(" advdata[%d]:%d ", i, data[i]);
        }
        if (startAdvSet.periodicdata_size() != 0) {
          for (uint16_t i = 0; i < startAdvSet.periodicdata_size(); i++)
            data.push_back(startAdvSet.periodicdata(i));
          for (uint16_t i = 0; i < data.size(); i++)
            ALOGD(" periodicdata[%d]:%d ", i, data[i]);
        }
        if (startAdvSet.scanresponse_size() != 0) {
          for (uint16_t i = 0; i < startAdvSet.scanresponse_size(); i++)
            data.push_back(startAdvSet.scanresponse(i));
          for (uint16_t i = 0; i < data.size(); i++)
            ALOGD(" scanresponse[%d]:%d ", i, data[i]);
        }

        adv_inst_id++;
        ALOGD("\n adv_inst_id: %d ", adv_inst_id);
        std::string encoded_bytes;
        ss_ble_on_advertising_set_started_event startAdvSetCb;
        startAdvSetCb.set_regid(reg_id);
        startAdvSetCb.set_advertiserid(adv_inst_id);
        startAdvSetCb.set_txpower(tx_power);
        startAdvSetCb.set_status(0);

        startAdvSetCb.SerializeToString(&encoded_bytes);
        uint16_t encoded_len = encoded_bytes.length();
        uint16_t status = FormRxPacket(BT_LE_ADVERTISING_SET_STARTED_EVENT,
                              PROTO_ENC_DEC, encoded_len, encoded_bytes);
        if (status != BT_STATUS_SUCCESS) {
          ALOGD(
              "\n FormRxPacket - BT_LE_ADVERTISING_SET_STARTED_EVENT failed "
              "with status :%d ",
              status);
        } else {
          ALOGD(
              "\n FormRxPacket - BT_LE_ADVERTISING_SET_STARTED_EVENT Success "
              "status :%d ",
              status);
        }
        break;
      }
      case BT_LE_ADV_UNREG: {
        ALOGD("\n In BT_LE_ADV_UNREG case ");
        ss_ble_stop_advertising_set stopAdvSet;
        stopAdvSet.ParseFromString(res_buffer);
        uint32_t advertiser_id = 0;
        if (stopAdvSet.has_advertiserid()) {
          advertiser_id = stopAdvSet.advertiserid();
          ALOGD("\n Removed Advertising Set id: %d ", advertiser_id);
        }
        adv_inst_id--;
        ALOGD("\n adv_inst_id : %d", adv_inst_id);
        break;
      }
      case BT_LE_ADV_ENABLE: {
        ALOGD("\n In BT_LE_ADV_ENABLE case ");
        ss_ble_enable_advertising_set enableAdvSet;
        enableAdvSet.ParseFromString(res_buffer);
        uint32_t advertiser_id = 0;
        uint32_t duration = 0;
        uint32_t maxextadvevents = 0;
        bool enabled = false;
        if (enableAdvSet.has_advertiserid()) {
          advertiser_id = enableAdvSet.advertiserid();
          ALOGD("\n advertiser id: %d ", advertiser_id);
        }
        if (enableAdvSet.has_enable()) {
          enabled = enableAdvSet.enable();
          ALOGD("\n enabled : %d ", enabled);
        }
        if (enableAdvSet.has_duration()) {
          duration = enableAdvSet.duration();
          ALOGD("\n duration : %d ", duration);
        }
        if (enableAdvSet.has_maxextadvevents()) {
          maxextadvevents = enableAdvSet.maxextadvevents();
          ALOGD("\n maxextadvevents : %d ", maxextadvevents);
        }
        std::string encoded_bytes;
        ss_ble_on_advertising_enabled_event onAdvEn;
        onAdvEn.set_advertiserid(advertiser_id);
        onAdvEn.set_enable(enabled);
        onAdvEn.set_status(0);

        onAdvEn.SerializeToString(&encoded_bytes);
        uint16_t encoded_len = encoded_bytes.length();
        uint16_t status = FormRxPacket(BT_LE_ADVERTISING_ENABLED_EVENT,
                              PROTO_ENC_DEC, encoded_len, encoded_bytes);
        if (status != BT_STATUS_SUCCESS) {
          ALOGD(
              "\n FormRxPacket - BT_LE_ADVERTISING_ENABLED_EVENT failed "
              "with status :%d ",
              status);
        } else {
          ALOGD(
              "\n FormRxPacket - BT_LE_ADVERTISING_ENABLED_EVENT Success "
              "status :%d ",
              status);
        }
        break;
      }
      case BT_LE_ADV_SET_DATA: {
        ALOGD("\n In BT_LE_ADV_SET_DATA case ");
        ss_ble_set_data setData;
        setData.ParseFromString(res_buffer);
        uint32_t advertiser_id = 0;
        bool scan_resp = false;
        std::vector<uint8_t> data;
        if (setData.has_advertiserid()) {
          advertiser_id = setData.advertiserid();
          ALOGD("\n advertiser id: %d ", advertiser_id);
        }
        if (setData.has_scanrespdata()) {
          scan_resp = setData.scanrespdata();
          ALOGD("\n scan_resp : %d ", scan_resp);
        }
        if (setData.advdata_size() != 0) {
          for (uint16_t i = 0; i < setData.advdata_size(); i++)
          data.push_back(setData.advdata(i));
          for (uint16_t i = 0; i < data.size(); i++)
           ALOGD(" advdata[%d]:%d " , i , data[i]);
        } 
        std::string encoded_bytes;
        ss_ble_on_advertising_data_set_event onAdvDataSet;
        onAdvDataSet.set_advertiserid(advertiser_id);
        onAdvDataSet.set_status(0);

        onAdvDataSet.SerializeToString(&encoded_bytes);
        uint16_t encoded_len = encoded_bytes.length();
        uint16_t status = BT_STATUS_SUCCESS;
        if (scan_resp != true) {
          status = FormRxPacket(BT_LE_ADVERTISING_DATA_SET_EVENT, PROTO_ENC_DEC,
                                encoded_len, encoded_bytes);
        } else {
          status = FormRxPacket(BT_LE_SCAN_RESP_DATA_SET_EVENT, PROTO_ENC_DEC,
                                encoded_len, encoded_bytes);
        }
        if (status != BT_STATUS_SUCCESS) {
          ALOGD(
              "\n FormRxPacket - "
              "ADVERTISING_DATA_SET_EVENT/SCAN_RESP_DATA_SET_EVENT failed "
              "with status :%d ",
              status);
        } else {
          ALOGD(
              "\n FormRxPacket - "
              "ADVERTISING_DATA_SET_EVENT/SCAN_RESP_DATA_SET_EVENT Success "
              "status :%d ",
              status);
        }
        break;
      }
      case BT_LE_ADV_SET_PARAM: {
        ALOGD("\n In BT_LE_ADV_SET_PARAM case ");
        ss_ble_set_advertising_parameters advSet;
        advSet.ParseFromString(res_buffer);
        uint32_t advertiser_id = 0;
        int32_t tx_power = 0;
        if (advSet.has_advertiserid()) {
          advertiser_id = advSet.advertiserid();
          ALOGD("\n advertiser id: %d ", advertiser_id);
        }
        if (advSet.has_parameters()) {
          ss_advertising_parameters adv_params = advSet.parameters();
          tx_power = adv_params.txpower();
          ALOGD("\n advertisingeventproperties : %d ", adv_params.advertisingeventproperties());
          ALOGD("\n mininterval : %d ", adv_params.mininterval());
          ALOGD("\n maxinterval : %d ", adv_params.maxinterval());
          ALOGD("\n channelmap : %d ", adv_params.channelmap());
          ALOGD("\n txpower : %d ", tx_power);
          ALOGD("\n primaryadvertisingphy : %d ", adv_params.primaryadvertisingphy());
          ALOGD("\n secondaryadvertisingphy : %d ", adv_params.secondaryadvertisingphy());
          ALOGD("\n scanrequestnotificationenable : %d ", adv_params.scanrequestnotificationenable());
        }
        std::string encoded_bytes;
        ss_ble_on_advertising_parameters_updated_event onAdvParam;
        onAdvParam.set_advertiserid(advertiser_id);
        onAdvParam.set_status(0);
        onAdvParam.set_txpower(tx_power);

        onAdvParam.SerializeToString(&encoded_bytes);
        uint16_t encoded_len = encoded_bytes.length();
        uint16_t status = FormRxPacket(BT_LE_ADV_PARAM_UPDATED_EVENT,
                              PROTO_ENC_DEC, encoded_len, encoded_bytes);
        if (status != BT_STATUS_SUCCESS) {
          ALOGD(
              "\n FormRxPacket - BT_LE_ADV_PARAM_UPDATED_EVENT failed "
              "with status :%d ",
              status);
        } else {
          ALOGD(
              "\n FormRxPacket - BT_LE_ADV_PARAM_UPDATED_EVENT Success "
              "status :%d ",
              status);
        }
        break;
      }
      case BT_LE_ADV_SET_PERIODIC_ADV_PARAM: {
        ALOGD("\n In BT_LE_ADV_SET_PERIODIC_ADV_PARAM case ");
        ss_ble_set_periodic_advertising_parameters setPeriodicAdvParam;
        setPeriodicAdvParam.ParseFromString(res_buffer);
        uint32_t advertiser_id = 0;
        int32_t tx_power = 0;
        if (setPeriodicAdvParam.has_advertiserid()) {
          advertiser_id = setPeriodicAdvParam.advertiserid();
          ALOGD("\n advertiser id: %d ", advertiser_id);
        }
        if (setPeriodicAdvParam.has_parameters()) {
          ss_periodic_advertising_parameters perioadv_params = setPeriodicAdvParam.parameters();
          ALOGD("\n periodicadvertisingproperties : %d ", perioadv_params.periodicadvertisingproperties());
          ALOGD("\n mininterval : %d ", perioadv_params.mininterval());
          ALOGD("\n maxinterval : %d ", perioadv_params.maxinterval());
          ALOGD("\n enable : %d ", perioadv_params.enable());
        }

        std::string encoded_bytes;
        ss_ble_on_periodic_advertising_parameters_updated_event
            onPeriodicAdvParam;
        onPeriodicAdvParam.set_advertiserid(advertiser_id);
        onPeriodicAdvParam.set_status(0);

        onPeriodicAdvParam.SerializeToString(&encoded_bytes);
        uint16_t encoded_len = encoded_bytes.length();
        uint16_t status = FormRxPacket(BT_LE_PERIODIC_ADV_PARAM_UPDATED_EVENT,
                              PROTO_ENC_DEC, encoded_len, encoded_bytes);
        if (status != BT_STATUS_SUCCESS) {
          ALOGD(
              "\n FormRxPacket - BT_LE_PERIODIC_ADV_PARAM_UPDATED_EVENT failed "
              "with status :%d ",
              status);
        } else {
          ALOGD(
              "\n FormRxPacket - BT_LE_PERIODIC_ADV_PARAM_UPDATED_EVENT Success "
              "status :%d ",
              status);
        }
        break;
      }
      case BT_LE_ADV_SET_PERIODIC_ADV_DATA: {
        ALOGD("\n In BT_LE_ADV_SET_PERIODIC_ADV_DATA case ");
        ss_ble_set_periodic_advertising_data setPeriodicAdvData;
        setPeriodicAdvData.ParseFromString(res_buffer);
        uint32_t advertiser_id = 0;
        std::vector<uint8_t> data;
        if (setPeriodicAdvData.has_advertiserid()) {
          advertiser_id = setPeriodicAdvData.advertiserid();
          ALOGD("\n advertiser id: %d ", advertiser_id);
        }
        if (setPeriodicAdvData.data_size() != 0) {
          for (uint16_t i = 0; i < setPeriodicAdvData.data_size(); i++)
           data.push_back(setPeriodicAdvData.data(i));
          for (uint16_t i = 0; i < data.size(); i++)
           ALOGD(" advdata[%d]:%d ", i, data[i]);
        }

        std::string encoded_bytes;
        ss_ble_on_periodic_advertising_data_set_event onPeriodicAdvDataSet;
        onPeriodicAdvDataSet.set_advertiserid(advertiser_id);
        onPeriodicAdvDataSet.set_status(0);

        onPeriodicAdvDataSet.SerializeToString(&encoded_bytes);
        uint16_t encoded_len = encoded_bytes.length();
        uint16_t status =
            FormRxPacket(BT_LE_PERIODIC_ADV_PARAM_UPDATED_EVENT, PROTO_ENC_DEC,
                         encoded_len, encoded_bytes);
        if (status != BT_STATUS_SUCCESS) {
          ALOGD(
              "\n FormRxPacket - BT_LE_PERIODIC_ADVERTISING_DATA_SET_EVENT "
              "failed "
              "with status :%d ",
              status);
        } else {
          ALOGD(
              "\n FormRxPacket - BT_LE_PERIODIC_ADVERTISING_DATA_SET_EVENT "
              "Success "
              "status :%d ",
              status);
        }
        break;
      }
      case BT_LE_ADV_SET_PERIODIC_ADV_ENABLE: {
        ALOGD("\n In BT_LE_ADV_SET_PERIODIC_ADV_ENABLE case ");
        ss_ble_set_periodic_advertising_enable setPeriodicAdvEn;
        setPeriodicAdvEn.ParseFromString(res_buffer);
        uint32_t advertiser_id = 0;
        bool enabled = false;
        if (setPeriodicAdvEn.has_advertiserid()) {
          advertiser_id = setPeriodicAdvEn.advertiserid();
          ALOGD("\n advertiser id: %d ", advertiser_id);
        }
        if (setPeriodicAdvEn.has_enable()) {
          enabled = setPeriodicAdvEn.enable();
          ALOGD("\n enabled : %d ", enabled);
        } 
        std::string encoded_bytes;
        ss_ble_on_periodic_advertising_enabled_event onPeriodicAdvEn;
        onPeriodicAdvEn.set_advertiserid(advertiser_id);
        onPeriodicAdvEn.set_enable(enabled);
        onPeriodicAdvEn.set_status(0);

        onPeriodicAdvEn.SerializeToString(&encoded_bytes);
        uint16_t encoded_len = encoded_bytes.length();
        uint16_t status = FormRxPacket(BT_LE_PERIODIC_ADVERTISING_ENABLED_EVENT,
                              PROTO_ENC_DEC, encoded_len, encoded_bytes);
        if (status != BT_STATUS_SUCCESS) {
          ALOGD(
              "\n FormRxPacket - BT_LE_PERIODIC_ADVERTISING_ENABLED_EVENT failed "
              "with status :%d ",
              status);
        } else {
          ALOGD(
              "\n FormRxPacket - BT_LE_PERIODIC_ADVERTISING_ENABLED_EVENT Success "
              "status :%d ",
              status);
        }
        break;
      }
      case BT_LE_GET_OWN_ADDRESS: {
        ALOGD("\n In BT_LE_GET_OWN_ADDRESS case ");
        ss_ble_get_own_address getOwnAdd;
        getOwnAdd.ParseFromString(res_buffer);
        ss_advertising_parameters* adv_params = NULL;  // advSet.parameters();
        uint32_t advertiser_id = 0;
        uint32_t addr_type = 0;

        if (getOwnAdd.has_advertiserid()) {
          advertiser_id = getOwnAdd.advertiserid();
          ALOGD("\n advertiser id: %d ", advertiser_id);
        }

        std::string encoded_bytes;
        ss_ble_on_own_address_read_event onOwnAddRead;
        onOwnAddRead.set_advertiserid(advertiser_id);
        onOwnAddRead.set_addresstype(0);
        onOwnAddRead.set_address(local_bd_addr.c_str());

        onOwnAddRead.SerializeToString(&encoded_bytes);
        uint16_t encoded_len = encoded_bytes.length();
        uint16_t status =
            FormRxPacket(BT_LE_OWN_ADDRESS_READ_EVENT, PROTO_ENC_DEC,
                         encoded_len, encoded_bytes);
        if (status != BT_STATUS_SUCCESS) {
          ALOGD(
              "\n FormRxPacket - BT_LE_OWN_ADDRESS_READ_EVENT failed "
              "with status :%d ",
              status);
        } else {
          ALOGD(
              "\n FormRxPacket - BT_LE_OWN_ADDRESS_READ_EVENT Success "
              "status :%d ",
              status);
        }
        break;
      }

      case BT_SDP_SEARCH:
      {
        ALOGI("Stub BT_SDP_SEARCH");
        std::string bd_addr = "22:22:8a:25:a8:9a";
        std::string uuid = "0000112f-0000-1000-8000-00805f9b34fb";
        uint8_t arr[5] = {3,4,56,7,8};
        int rec_length = 5;
        std::string rec_data;
        rec_data = (char*)arr;
        std::string name = "OBEX Phonebook Access Server";

        uint8_t sdp_msg[MAX_LENGTH_WITH_PROTO_NONE];
        //adding msg_id
        uint16_t MSG_ID = BT_SDP_SEARCH_COMPLETE_CB;
        sdp_msg[0] = MSG_ID & 0xff;
        sdp_msg[1] = (MSG_ID >> 8);

        std::string protoMsg;
        ss_sdp_search_complete_callback sdpSearchCb;
        sdpSearchCb.set_status(bluetooth::synergy::SynergyProto::SS_BT_STATUS_SUCCESS_t);
        sdpSearchCb.set_remote_bd_addr(bd_addr.c_str());
        sdpSearchCb.set_uuid(uuid.c_str());
        //sdpSearchCb.set_record_length(rec_length);
        //sdpSearchCb.set_record_data(rec_data.c_str());
        sdpSearchCb.set_record_count(1);
        ss_bt_sdp_record* raw_record = sdpSearchCb.mutable_record();
        ss_bt_sdp_pse_record* pse_record = raw_record->mutable_pse();
        ss_bt_sdp_hdr_overlay* record = pse_record->mutable_hdr();
        record->set_type(SS_BT_SDP_TYPE_PBAP_PSE);
        record->set_uuid(uuid.c_str());
        record->set_service_name_length(strlen(name.c_str()));
        ss_bt_service_name* s_name = record->mutable_service_name();
        s_name->set_name(name.c_str());
        record->set_rfcomm_channel_number(19);
        record->set_l2cap_psm(-1);
        record->set_profile_version(257);
        pse_record->set_supported_features(3);
        pse_record->set_supported_repositories(3);
        //record->set_record_length(rec_length);
        //record->set_record_data(rec_data.c_str());
        //sdpSearchCb.set_record(record);
        ALOGI("Send sdp search complete callback");
        sdpSearchCb.SerializeToString(&protoMsg);
        ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());
        //adding length
        uint16_t length = protoMsg.length();
        sdp_msg[2] = length & 0xff;
        sdp_msg[3] = (length >> 8);
        //adding proto_encode
        uint16_t proto_encode = PROTO_ENC_DEC;
        sdp_msg[4] = proto_encode & 0xff;
        sdp_msg[5] = (proto_encode >> 8);
        char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
        memcpy(resBuffer, (char *) sdp_msg, MAX_LENGTH_WITH_PROTO_NONE);
        std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
        msgStr.append(protoMsg);
        tBTIF_SS_Cback ss_cback;
        memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
        ss_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + length)*sizeof(uint8_t));
        //This memory should be released from each profile after done with the processing
        std::vector<uint8_t> protoVector(msgStr.begin(), msgStr.end());
        uint8_t *proto_msg = &protoVector[0];
        memcpy(ss_cback.payload, proto_msg, ((MAX_LENGTH_WITH_PROTO_NONE + length) * sizeof(uint8_t)));
        btif_transfer_context(btif_sdp_ss_callback, MSG_ID, (char*)&ss_cback, sizeof(ss_cback),NULL);
        break;
      }

      case BT_SDP_CREATE_RECORD:
      {
        ALOGI("Stub BT_SDP_SEARCH");
        uint32_t handle = 1;

        uint8_t sdp_msg[MAX_LENGTH_WITH_PROTO_NONE];
        //adding msg_id
        uint16_t MSG_ID = BT_SDP_SEARCH_COMPLETE_CB;
        sdp_msg[0] = MSG_ID & 0xff;
        sdp_msg[1] = (MSG_ID >> 8);

        std::string protoMsg;
        ss_bt_create_sdp_record createSdpRecord;
        createSdpRecord.ParseFromString(res_buffer);
        if (createSdpRecord.has_handle()) {
          handle = createSdpRecord.handle();
          ALOGD("has record handle : %d", handle);
        }
        if (createSdpRecord.has_record()) {

          int record_length = 0;
          uint8_t* rec_data = NULL;
          std::string data;
          ss_bt_sdp_type record_type;
          ss_bt_service_name* s_name;
          int serv_name_len;
          int rfcomm_chnl_no;
          int l2cap_psm;
          int profile_version;

          //ss_bt_sdp_record rec = createSdpRecord.record();
          ss_bt_sdp_record* rec = createSdpRecord.mutable_record();

          switch(handle){
            case 2:
            case 3:
              ALOGD("Create PCE record");
              std::string uuid = "0000112f-0000-1000-8000-00805f9b34fb";
              ss_bt_sdp_pce_record* pce_record = rec->mutable_pce();
              ss_bt_sdp_hdr_overlay* pce_hdr = pce_record->mutable_hdr();
              //ss_bt_sdp_pce_record pce_rec = rec.mutable_pce();
              //ss_bt_sdp_hdr_overlay pce_hdr = pce_rec.mutable_hdr();
              pce_hdr->set_uuid(uuid.c_str());
              ALOGD("uuid set to : %s", uuid.c_str());

              if(pce_hdr->has_type()){
              record_type  = pce_hdr->type();
              ALOGD("type : %d", record_type);
              }
              if(pce_hdr->has_service_name_length()){
              serv_name_len  = pce_hdr->service_name_length();
              ALOGD("service name length : %d", serv_name_len);
              }
              if(pce_hdr->has_service_name()){
              s_name  = pce_hdr->mutable_service_name();
              ALOGD("service name : %s", (char*)s_name);
              }
              if(pce_hdr->has_rfcomm_channel_number()){
              rfcomm_chnl_no = pce_hdr->rfcomm_channel_number();
              ALOGD("rfcomm_channel_number : %d", handle);
              }
              if(pce_hdr->has_l2cap_psm()){
              l2cap_psm = pce_hdr->l2cap_psm();
              ALOGD("has l2cap_psm : %d", l2cap_psm);
              }
              if(pce_hdr->has_profile_version()){
              profile_version = pce_hdr->profile_version();
              ALOGD("has profile version : %d", profile_version);
              }
          }


        }
        ALOGI("Stub: BT_SDP_CREATE_RECORD: Record created");
        //adding length
        uint16_t length = protoMsg.length();
        sdp_msg[2] = length & 0xff;
        sdp_msg[3] = (length >> 8);
        //adding proto_encode
        uint16_t proto_encode = PROTO_ENC_DEC;
        sdp_msg[4] = proto_encode & 0xff;
        sdp_msg[5] = (proto_encode >> 8);
        char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
        memcpy(resBuffer, (char *) sdp_msg, MAX_LENGTH_WITH_PROTO_NONE);
        std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
        msgStr.append(protoMsg);
        break;
      }

      case BT_SDP_REMOVE_RECORD:
      {
        ALOGI("Stub: BT_REMOVE_RECORD");
        ALOGI("sTUB: RECORD_REMOVED");
        std::string bd_addr = "22:22:8a:25:a8:9a";
        std::string uuid = "0000112f-0000-1000-8000-00805f9b34fb";
        uint8_t arr[5] = {3,4,56,7,8};
        int rec_length = 5;
        std::string rec_data;
        rec_data = (char*)arr;

        uint8_t sdp_msg[MAX_LENGTH_WITH_PROTO_NONE];
        //adding msg_id
        uint16_t MSG_ID = BT_SDP_SEARCH_COMPLETE_CB;
        sdp_msg[0] = MSG_ID & 0xff;
        sdp_msg[1] = (MSG_ID >> 8);

        std::string protoMsg;


        ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());
        //adding length
        uint16_t length = protoMsg.length();
        sdp_msg[2] = length & 0xff;
        sdp_msg[3] = (length >> 8);
        //adding proto_encode
        uint16_t proto_encode = PROTO_ENC_DEC;
        sdp_msg[4] = proto_encode & 0xff;
        sdp_msg[5] = (proto_encode >> 8);
        char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
        memcpy(resBuffer, (char *) sdp_msg, MAX_LENGTH_WITH_PROTO_NONE);
        std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
        msgStr.append(protoMsg);
        break;
      }

      default:
      ALOGI("msg_id : %d Not matching with any DM event",msg_id);
      break;
  }
}

void BluetoothSSStubInterface::sendDummydeviceCallback() {
    ALOGI("%s", __func__);
    bt_property_t properties[4];
    memset(properties, 0, sizeof(properties));
	
    int dev_type = (int)BT_DEVICE_DEVTYPE_DUAL;
    int8_t rssi = 10;
	
    std::string bt_addstr = "22:11:33:44:55:66";  
    uint8_t bd_addr[249];
    strlcpy((char*)bd_addr, (char*)bt_addstr.c_str(), sizeof(bd_addr));
    properties[0].len = sizeof(bd_addr);
    properties[0].val = &bd_addr;
    properties[0].type = BT_PROPERTY_BDADDR;
	
    std::string bt_name  = "Amisha dummy device";
    bt_bdname_t bd_name;
    strlcpy((char*)bd_name.name, (char*)bt_name.c_str(), sizeof(bt_bdname_t));
    properties[1].len = sizeof(bd_name);
    properties[1].val = &bd_name;
    properties[1].type = BT_PROPERTY_BDNAME;

    std::string rssi_str = std::to_string(rssi);
    uint8_t rssi_val[249];
    strlcpy((char*)rssi_val, (char*)rssi_str.c_str(), sizeof(rssi_val));
	
    std::string dev_str = std::to_string(dev_type);
    uint8_t dev_val[249];
    strlcpy((char*)dev_val, (char*)dev_str.c_str(), sizeof(dev_val));
			
    properties[2].len = sizeof(dev_val);
    properties[2].val = &dev_val;
    properties[2].type = BT_PROPERTY_TYPE_OF_DEVICE;
	
    properties[3].len = sizeof(rssi_val);
    properties[3].val = &rssi_val;
    properties[3].type = BT_PROPERTY_REMOTE_RSSI;

    ALOGI("%s : now add the device properties to callback", __func__);

    uint8_t device_found_msg[MAX_LENGTH_WITH_PROTO_NONE];
    //adding msg_id
    uint16_t msg_id = BT_DM_DEVICE_FOUND_CB;
    device_found_msg[0] = msg_id & 0xff;
    device_found_msg[1] = (msg_id >> 8);

    std::string protoMsg;
    ss_bt_property_t* bt_prop[4];
    ss_device_found_callback devicefoundcb;
    devicefoundcb.set_num_properties(4);
    for(int i=0;i<4;i++)
    {
      bt_property_t property = properties[i];
      ALOGI("%s : now adding property type - %d",__func__, property.type);
      bt_prop[i] = devicefoundcb.add_properties(); 
      bt_prop[i]->set_type((ss_bt_property_type_t)property.type);
      bt_prop[i]->set_len(property.len);
      bt_prop[i]->set_val((char*)property.val);
    }
    devicefoundcb.SerializeToString(&protoMsg);
    ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());

    uint16_t length = protoMsg.length();
        device_found_msg[2] = length & 0xff;
        device_found_msg[3] = (length >> 8);
        //adding proto_encode
        uint16_t proto_encode = PROTO_ENC_DEC;
        device_found_msg[4] = proto_encode & 0xff;
        device_found_msg[5] = (proto_encode >> 8);
        char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
        memcpy(resBuffer, (char *) device_found_msg, MAX_LENGTH_WITH_PROTO_NONE);
        std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
        msgStr.append(protoMsg);

        tBTIF_SS_Cback ss_cback;
        memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
        ss_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + length)*sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
        std::vector<uint8_t> protoVector(msgStr.begin(), msgStr.end());
        uint8_t *proto_msg = &protoVector[0];
        memcpy(ss_cback.payload, proto_msg, ((MAX_LENGTH_WITH_PROTO_NONE + length) * sizeof(uint8_t)) );

        btif_transfer_context(btif_dm_ss_callback, msg_id, (char*)&ss_cback, sizeof(ss_cback),NULL);
        return;
}

void BluetoothSSStubInterface::sendDummyAdapterPropCallback() {
    ALOGI("%s", __func__);
    bt_property_t properties[2];
    memset(properties, 0, sizeof(properties));

    std::string bt_addstr = "22:22:11:44:55:67";  
    uint8_t bd_addr[249];
    strlcpy((char*)bd_addr, (char*)bt_addstr.c_str(), sizeof(bd_addr));
    properties[0].len = sizeof(bd_addr);
    properties[0].val = &bd_addr;
    properties[0].type = BT_PROPERTY_BDADDR;
	
    std::string bt_name  = "MEP_SS_device";
    bt_bdname_t bd_name;
    strlcpy((char*)bd_name.name, (char*)bt_name.c_str(), sizeof(bt_bdname_t));
    properties[1].len = sizeof(bd_name);
    properties[1].val = &bd_name;
    properties[1].type = BT_PROPERTY_BDNAME;

    ALOGI("%s : now add the adapter properties to callback", __func__);

    uint8_t adap_prop_msg[MAX_LENGTH_WITH_PROTO_NONE];
    //adding msg_id
    uint16_t msg_id = BT_DM_ADAPTER_PROPERTIES_CB;
    adap_prop_msg[0] = msg_id & 0xff;
    adap_prop_msg[1] = (msg_id >> 8);

    std::string protoMsg;
    ss_adapter_properties_callback devicefoundcb;
    devicefoundcb.set_status(bluetooth::synergy::SynergyProto::SS_BT_STATUS_SUCCESS);
    devicefoundcb.set_num_properties(2);
    ss_bt_property_t* bt_prop[2] ;
    for(int i=0;i<2;i++)
    {
      bt_property_t property = properties[i];
      ALOGI("%s : now adding property type - %d", __func__, property.type);
      bt_prop[i] = devicefoundcb.add_properties();
      bt_prop[i]->set_type((ss_bt_property_type_t)property.type);
      bt_prop[i]->set_len(property.len);
      bt_prop[i]->set_val((char*)property.val);
    }
    devicefoundcb.SerializeToString(&protoMsg);
    ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());

    uint16_t length = protoMsg.length();
        adap_prop_msg[2] = length & 0xff;
        adap_prop_msg[3] = (length >> 8);
        //adding proto_encode
        uint16_t proto_encode = PROTO_ENC_DEC;
        adap_prop_msg[4] = proto_encode & 0xff;
        adap_prop_msg[5] = (proto_encode >> 8);
        char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
        memcpy(resBuffer, (char *) adap_prop_msg, MAX_LENGTH_WITH_PROTO_NONE);
        std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
        msgStr.append(protoMsg);

        tBTIF_SS_Cback ss_cback;
        memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
        ss_cback.payload = (uint8_t *)malloc((MAX_LENGTH_WITH_PROTO_NONE + length)*sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
        std::vector<uint8_t> protoVector(msgStr.begin(), msgStr.end());
        uint8_t *proto_msg = &protoVector[0];
        memcpy(ss_cback.payload, proto_msg, ((MAX_LENGTH_WITH_PROTO_NONE + length) * sizeof(uint8_t)) );

        btif_transfer_context(btif_dm_ss_callback, msg_id, (char*)&ss_cback, sizeof(ss_cback),NULL);
        return;
}
#endif //SS_STUB_ENABLED
