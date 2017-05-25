/*********************************************************************
 * INCLUDES
 */
#include "OSAL.h"
#include "AF.h"
#include "ZDApp.h"
#include "ZDObject.h"
#include "ZDProfile.h"
#include "string.h"

#include "Coordinator.h"
#include "DebugTrace.h"

#if !defined( WIN32 ) || defined( ZBIT )
  #include "OnBoard.h"
#endif

/* HAL */
#include "hal_lcd.h"
#include "hal_led.h"
#include "hal_key.h"
#include "hal_uart.h"
int rxlen;
unsigned char uartbuf[50];
void rxCB(uint8 port,uint8 event);
// This list should be filled with Application specific Cluster IDs.
const cId_t GenericApp_ClusterList[GENERICAPP_MAX_CLUSTERS] =
{
  GENERICAPP_CLUSTERID
};

const SimpleDescriptionFormat_t GenericApp_SimpleDesc =
{
  GENERICAPP_ENDPOINT,              //  int Endpoint;
  GENERICAPP_PROFID,                //  uint16 AppProfId[2];
  GENERICAPP_DEVICEID,              //  uint16 AppDeviceId[2];
  GENERICAPP_DEVICE_VERSION,        //  int   AppDevVer:4;
  GENERICAPP_FLAGS,                 //  int   AppFlags:4;
  GENERICAPP_MAX_CLUSTERS,          //  byte  AppNumInClusters;
  (cId_t *)GenericApp_ClusterList,  //  byte *pAppInClusterList;
  GENERICAPP_MAX_CLUSTERS,          //  byte  AppNumInClusters;
  (cId_t *)GenericApp_ClusterList   //  byte *pAppInClusterList;
};

endPointDesc_t GenericApp_epDesc;

byte GenericApp_TaskID;   // Task ID for internal task/event processing
                          // This variable will be received when
                          // GenericApp_Init() is called.

devStates_t GenericApp_NwkState;

byte GenericApp_TransID;  // This is the unique message ID (counter)

afAddrType_t GenericApp_DstAddr;

// Number of recieved messages

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void GenericApp_MessageMSGCB( afIncomingMSGPacket_t *pckt );
static void GenericApp_SendTheMessage( void );
/*Appc层的初始化函数，主要完成了串口的配置*/
void GenericApp_Init( uint8 task_id )
{
  GenericApp_TaskID = task_id;
  GenericApp_NwkState = DEV_INIT;
  GenericApp_TransID = 0;
  halUARTCfg_t uartConfig; // 关于串口配置的结构体

  GenericApp_DstAddr.addrMode = (afAddrMode_t)AddrNotPresent;
  GenericApp_DstAddr.endPoint = 0;
  GenericApp_DstAddr.addr.shortAddr = 0;

  // Fill out the endpoint description.
  GenericApp_epDesc.endPoint = GENERICAPP_ENDPOINT;
  GenericApp_epDesc.task_id = &GenericApp_TaskID;
  GenericApp_epDesc.simpleDesc
            = (SimpleDescriptionFormat_t *)&GenericApp_SimpleDesc;
  GenericApp_epDesc.latencyReq = noLatencyReqs;
  
  uartConfig.configured = TRUE;
  uartConfig.baudRate = HAL_UART_BR_115200;
  uartConfig.flowControl = FALSE;
  uartConfig.flowControlThreshold = 1; 
  uartConfig.rx.maxBufSize        = 255;  
  uartConfig.tx.maxBufSize        = 255;  
  uartConfig.idleTimeout          = 1;                
  uartConfig.intEnable            = TRUE;    
  uartConfig.callBackFunc =rxCB;
  HalUARTOpen (0,&uartConfig);//对串口0进行初始化
  // Register the endpoint description with the AF
  afRegister( &GenericApp_epDesc );
  // Register for all key events - This app will handle all key events
  RegisterForKeys( GenericApp_TaskID );

  ZDO_RegisterForZDOMsg( GenericApp_TaskID, End_Device_Bind_rsp );
  ZDO_RegisterForZDOMsg( GenericApp_TaskID, Match_Desc_rsp );

}
/*APP层的消息处理函数*/
uint16 GenericApp_ProcessEvent( uint8 task_id, uint16 events )
{
  afIncomingMSGPacket_t *MSGpkt;

  // Data Confirmation message fields

  (void)task_id;  // Intentionally unreferenced parameter

  if ( events & SYS_EVENT_MSG )
  {
    MSGpkt = (afIncomingMSGPacket_t *)osal_msg_receive( GenericApp_TaskID );
    while ( MSGpkt )
    {
      switch ( MSGpkt->hdr.event )
      {
        case AF_INCOMING_MSG_CMD://接收到新的无线数据
          HalLedSet(HAL_LED_2, HAL_LED_MODE_BLINK);
          GenericApp_MessageMSGCB( MSGpkt );
          break;

        case ZDO_STATE_CHANGE: //网络状态改变
          GenericApp_NwkState = (devStates_t)(MSGpkt->hdr.status);
          if ( (GenericApp_NwkState == DEV_ZB_COORD) ||
               (GenericApp_NwkState == DEV_ROUTER) ||
               (GenericApp_NwkState == DEV_END_DEVICE) )
          {
            // Start sending "the" message in a regular interval.
           HalLedSet(HAL_LED_1, HAL_LED_MODE_FLASH);
          }
          break;

        default:
          break;
      }
      // Release the memory
      osal_msg_deallocate( (uint8 *)MSGpkt );
      // Next
      MSGpkt = (afIncomingMSGPacket_t *)osal_msg_receive( GenericApp_TaskID );
    }
    // return unprocessed events
    return (events ^ SYS_EVENT_MSG);
  }
  
  if ( events & GENERICAPP_SEND_MSG_EVT )
  {
    GenericApp_SendTheMessage();
    return (events ^ GENERICAPP_SEND_MSG_EVT);
  }
  return 0;
}

/*对接收到的数据进行处理的函数*/
static void GenericApp_MessageMSGCB( afIncomingMSGPacket_t *pkt )
{
  uint8 sendBuff[50];  //定义一个发送的数据，
  switch ( pkt->clusterId )
  {
    case GENERICAPP_CLUSTERID:
      HalLedSet ( HAL_LED_2, HAL_LED_MODE_BLINK );  // Blink an LED
      osal_memcpy(&sendBuff,pkt->cmd.Data,sizeof(sendBuff));
      HalUARTWrite(0,(uint8*)&sendBuff,sizeof(sendBuff));
      memset(sendBuff,'\0',50);
      break;
  }
}
/*发送数据的函数*/
static void GenericApp_SendTheMessage( void )
{
  char theMessageData[50];
  osal_memcpy(&theMessageData,&uartbuf,sizeof(theMessageData));
  afAddrType_t my_DstAddr; //设备描述符
  my_DstAddr.addrMode = (afAddrMode_t)Addr16Bit; //单播发送
  my_DstAddr.endPoint = GENERICAPP_ENDPOINT;
  my_DstAddr.addr.shortAddr = 0x0000; //发送到协调器
  if ( AF_DataRequest( &my_DstAddr, &GenericApp_epDesc,
                       GENERICAPP_CLUSTERID,
                       (byte)osal_strlen( theMessageData ) + 1,
                       (byte *)&theMessageData,
                       &GenericApp_TransID,
                       AF_DISCV_ROUTE, AF_DEFAULT_RADIUS ) == afStatus_SUCCESS )
  {
    memset(theMessageData,'\0',50);
    memset(uartbuf,'\0',50);
    HalLedBlink(HAL_LED_2, 2, HAL_LED_DEFAULT_DUTY_CYCLE, HAL_LED_DEFAULT_FLASH_TIME);
  }
  else
  {
    //发送失败，暂时不处理
  }
}
/*串口回调函数*/
void rxCB(uint8 port,uint8 event)
{  
  if(event == 0x04)
  {
   memset(uartbuf,'\0',50);
   rxlen = Hal_UART_RxBufLen(HAL_UART_PORT_0); //读取到的数据长度
   HalUARTRead(0, uartbuf,sizeof(uartbuf)); //读取串口数据
   //设置自定义的事件发生。
   osal_set_event(GenericApp_TaskID,GENERICAPP_SEND_MSG_EVT);
  }
}