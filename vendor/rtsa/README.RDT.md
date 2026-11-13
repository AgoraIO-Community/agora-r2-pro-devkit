# 1. RDT简介

RDT是 ‘Reliable Data Transmission’ 的简称，是rtsa SDK提供的一个用于可靠数据传输的通道，可以在同一个频道中实现两个uid之间的点到点可靠数据通信。

RDT通道支持发送两种类型的消息CMD和DATA，两种消息的枚举定义如下：
```
/**
 * Reliable Data Transmission Tunnel message type
 */
typedef enum rdt_stream_type {
  RDT_STREAM_CMD,    // Reliable; High priority; Limit 256 bytes per frame, 100 packets per second
  RDT_STREAM_DATA,   // Reliable; Low priority; Restricted by congestion control; Limit 128K bytes per frame
} rdt_stream_type_e;
```
简单来说：
- CMD : 用于控制消息发送。发送优先级高，不受拥塞控制策略影响，不会阻塞发送，但限制每秒发送100个包，每包长度256字节
- DATA: 用于数据消息发送。发送优先级低，受拥塞控制策略影响，发送缓冲区满时会返回失败，但不限制发包数量，每包限制大小128K字节

# 2. RDT接口
## 2.1 状态接口
```
/**
 * The definition of the rtc_channel_options_t struct.
 */
typedef struct {
  ...
  省略字段
  ...

  // auto build Reliable-Data-Transmission-Tunnel with uid who goes online
  bool enable_rdt;
} rtc_channel_options_t;

/**
 * Reliable Data Transmission tunnel state
 */
typedef enum rdt_state {
  RDT_STATE_CLOSED,  // initial or closed
  RDT_STATE_OPENED,  // opened and can send data
  RDT_STATE_BLOCKED, // send buffer is full, can't send data, but can send cmd
  RDT_STATE_PENDING, // reconnecting tunnel, can't send data
  RDT_STATE_BROKEN,  // rdt tunnel broken, will auto reset and rebuild tunnel
} rdt_state_e;

/**
  * Occur when user rdt state changed
  * @param[in] conn_id  Connection identification
  * @param[in] uid      Remote user ID
  * @param[in] state    Rdt tunnel state
  */
void (*on_rdt_state)(connection_id_t conn_id, uint32_t uid, rdt_state_e state);
```

- RDT通道的使用需要SDK先加入频道，频道选项中rtc_channel_options_t.enable_rdt需要设置为true，表示使能RDT功能
- 当频道中有其它uid加入时，SDK会主动与其建立RDT连接
- 当与某uid建立好rdt连接后，会触发on_rdt_state回调, state=RDT_STATE_OPENED，表示可以收发数据
- 对于CMD类型消息，在处于RDT_STATE_OPENED和RDT_STATE_BLOCKED时都可以发送
- 对于DATA类型消息，只有处于RDT_STATE_OPENED才能发送

## 2.2 发送接口

```
/**
 * Reliable Data Transmission Tunnel message type
 */
typedef enum rdt_stream_type {
  RDT_STREAM_CMD,    // Reliable; High priority; Limit 256 bytes per packet, 100 packets per second
  RDT_STREAM_DATA,   // Reliable; Low priority; Restricted by congestion control; Limit 1024 bytes per packet
} rdt_stream_type_e;

/**
 * Send Reliable message to remote uid in channel
 *
 * @param[in] conn_id       Connection identification
 * @param[in] remote_uid    Remote user ID
 * @param[in] type          Reliable Data Transmission tunnel message type
 * @param[in] msg           Message's payload buffer
 * @param[in] length        Message's payload buffer length (cmd: max 256 bytes, data: max 1024 bytes)
 *
 * @return
 * - = 0: Success
 * - < 0: Failure
 */
extern __agora_api__ int agora_rtc_send_rdt_msg(connection_id_t conn_id, uint32_t remote_uid, rdt_stream_type_e type, const void *msg, size_t length);
```

- 只有在处于RDT_STATE_OPENED状态才能发送数据
- 对于RDT_STREAM_CMD类型，优先更高，限制每秒发送100个包，每包256Bytes
- 对于RDT_STREAM_DATA类型，不限制发包个数，限制每包1024Bytes
- 对于RDT_STREAM_CMD类型消息，在处于RDT_STATE_OPENED和RDT_STATE_BLOCKED时都可以发送
- 对于RDT_STREAM_DATA类型消息，只有处于RDT_STATE_OPENED才能发送

## 2.3 回调接口
```
/**
  * Occur when receive rdt message from uid
  * @param[in] conn_id  Connection identification
  * @param[in] uid      Remote user ID
  * @param[in] type     Rdt message type
  * @param[in] msg      Rdt message content
  * @param[in] len      Rdt message length
  */
void (*on_rdt_msg)(connection_id_t conn_id, uint32_t uid, rdt_stream_type_e type, const void *msg, size_t len);
```

注册该回调后，可以收到与某uid建立好的rdt通道的数据

# 3. RDT示例
参考SDK包中的hello_rdt。
- 发送：./hello_rdt -i $APPID -t $TOKEN -c zgx -u 1 -p 2 -s
- 接收：./hello_rdt -i $APPID -t $TOKEN -c zgx -u 2 -p 1