/**
 *  @file       fifo_buf.h
 *  @author     guangqi.rong
 *  @version    v1.0.0
 *  @brief      轻量级 FIFO 环形缓冲队列
 *  @note       适用于串口、SPI、网络等通信口的数据缓冲
 *              仅支持单生产者单消费者场景
 */

#ifndef FIFO_BUF_H
#define FIFO_BUF_H

#include <stdint.h>
#include <stdbool.h>

/* 溢出模式定义 */
#define FIFO_OVERFLOW_OVERWRITE  0  /* 覆盖旧数据 */
#define FIFO_OVERFLOW_BLOCK      1  /* 等待空间 */

/* 默认溢出模式 */
#ifndef FIFO_OVERFLOW_MODE
#define FIFO_OVERFLOW_MODE FIFO_OVERFLOW_BLOCK
#endif

/* 错误码定义 */
#define FIFO_OK                 0
#define FIFO_ERR_NULL          -1
#define FIFO_ERR_SIZE          -2
#define FIFO_ERR_ALIGN         -3
#define FIFO_ERR_FULL          -4
#define FIFO_ERR_EMPTY         -5

/*========================================
 *  数据结构定义
 *========================================*/

/**
 * @brief FIFO 环形缓冲区结构体
 * @note  仅支持单生产者单消费者无锁操作
 */
typedef struct {
    volatile uint32_t head;   /**< 写入位置（生产者更新） */
    volatile uint32_t tail;   /**< 读取位置（消费者更新） */
    uint32_t size;            /**< 缓冲区大小（必须是 2 的幂） */
    uint32_t mask;            /**< 掩码 = size - 1 */
    uint8_t *buf;             /**< 数据缓冲区指针 */
} fifo_buf_t;

/*========================================
 *  静态分配宏
 *========================================*/

/**
 * @brief 静态声明 FIFO 缓冲区
 * @param name  变量名
 * @param sz    缓冲区大小（会自动向上取整到 2 的幂）
 */
#define FIFO_BUF_STATIC(name, sz) \
    static uint8_t name##_buf__[((sz) < 16 ? 16 : (sz))]; \
    static fifo_buf_t name = { \
        .head = 0, \
        .tail = 0, \
        .size = 0, \
        .mask = 0, \
        .buf = name##_buf__ \
    }

/*========================================
 *  公开接口声明
 *========================================*/

/**
 * @brief       初始化 FIFO 缓冲区（外部缓冲区）
 * @param[out]  fifo   FIFO 结构体指针
 * @param[in]  buf    外部缓冲区指针（必须 4 字节对齐）
 * @param[in]  size   缓冲区大小
 * @return      FIFO_OK 成功，其他为错误码
 */
int fifo_buf_init(fifo_buf_t *fifo, uint8_t *buf, uint32_t size);

/**
 * @brief       初始化 FIFO 缓冲区（静态分配）
 * @param[out]  fifo   FIFO 结构体指针
 * @param[in]  size   缓冲区大小
 * @return      FIFO_OK 成功，其他为错误码
 */
int fifo_buf_init_static(fifo_buf_t *fifo, uint32_t size);

/**
 * @brief       重置 FIFO 缓冲区
 * @param[in/out] fifo FIFO 结构体指针
 */
void fifo_buf_reset(fifo_buf_t *fifo);

/**
 * @brief       获取缓冲区实际大小
 * @param[in]  fifo   FIFO 结构体指针
 * @return      缓冲区大小（字节）
 */
uint32_t fifo_buf_size(const fifo_buf_t *fifo);

/*========================================
 *  写入接口
 *========================================*/

/**
 * @brief       写入数据到 FIFO
 * @param[in/out] fifo FIFO 结构体指针
 * @param[in]  data   数据指针
 * @param[in]  len    数据长度
 * @return      FIFO_OK 成功，FIFO_ERR_FULL 缓冲区满
 */
int fifo_buf_write(fifo_buf_t *fifo, const uint8_t *data, uint32_t len);

/**
 * @brief       从 ISR 写入数据到 FIFO
 * @param[in/out] fifo FIFO 结构体指针
 * @param[in]  data   数据指针
 * @param[in]  len    数据长度
 * @return      FIFO_OK 成功，FIFO_ERR_FULL 缓冲区满
 */
int fifo_buf_write_isr(fifo_buf_t *fifo, const uint8_t *data, uint32_t len);

/*========================================
 *  读取接口
 *========================================*/

/**
 * @brief       从 FIFO 读取数据（零拷贝）
 * @param[in/out] fifo FIFO 结构体指针
 * @param[out] len    返回数据长度
 * @return      数据指针，NULL 表示无数据
 * @note        使用完毕后必须调用 fifo_buf_read_done()
 */
uint8_t *fifo_buf_read(fifo_buf_t *fifo, uint32_t *len);

/**
 * @brief       释放已读取的数据
 * @param[in/out] fifo FIFO 结构体指针
 */
void fifo_buf_read_done(fifo_buf_t *fifo);

/**
 * @brief       从 FIFO 读取数据（拷贝）
 * @param[in/out] fifo FIFO 结构体指针
 * @param[out] data   目标缓冲区
 * @param[in]  max_len 最大读取长度
 * @return      实际读取的字节数
 */
uint32_t fifo_buf_read_copy(fifo_buf_t *fifo, uint8_t *data, uint32_t max_len);

/**
 * @brief       从 ISR 读取数据（零拷贝）
 * @param[in/out] fifo FIFO 结构体指针
 * @param[out] len    返回数据长度
 * @return      数据指针，NULL 表示无数据
 */
uint8_t *fifo_buf_read_isr(fifo_buf_t *fifo, uint32_t *len);

/*========================================
 *  状态查询接口
 *========================================*/

/**
 * @brief       查询可用数据量
 * @param[in]  fifo   FIFO 结构体指针
 * @return      可用数据字节数
 */
uint32_t fifo_buf_available(const fifo_buf_t *fifo);

/**
 * @brief       查询剩余空间
 * @param[in]  fifo   FIFO 结构体指针
 * @return      剩余空间字节数
 */
uint32_t fifo_buf_free(const fifo_buf_t *fifo);

/**
 * @brief       判断缓冲区是否为空
 * @param[in]  fifo   FIFO 结构体指针
 * @return      true=空，false=非空
 */
bool fifo_buf_is_empty(const fifo_buf_t *fifo);

/**
 * @brief       判断缓冲区是否已满
 * @param[in]  fifo   FIFO 结构体指针
 * @return      true=满，false=未满
 */
bool fifo_buf_is_full(const fifo_buf_t *fifo);

/**
 * @brief       预览数据（不移动读指针）
 * @param[in]  fifo   FIFO 结构体指针
 * @param[out] len    返回数据长度
 * @return      数据指针，NULL 表示无数据
 * @note        数据仍然在缓冲区中，调用后需手动调用 fifo_buf_read_done()
 */
uint8_t *fifo_buf_peek(const fifo_buf_t *fifo, uint32_t *len);

#endif /* FIFO_BUF_H */
