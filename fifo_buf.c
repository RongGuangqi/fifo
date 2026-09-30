/**
 *  @file       fifo_buf.c
 *  @author     guangqi.rong
 *  @version    v1.0.0
 *  @brief      轻量级 FIFO 环形缓冲队列实现
 */

#include <string.h>
#include <stdint.h>

#include "fifo_buf.h"

#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
#include "freertos/FreeRTOS.h"
#include "esp_cpu.h"
#define ENTER_CRITICAL()  portENTER_CRITICAL(NULL)
#define EXIT_CRITICAL()   portEXIT_CRITICAL(NULL)
#else
#define ENTER_CRITICAL()  __asm__ volatile("csrrci mstatus, 8" ::: "memory")
#define EXIT_CRITICAL()   __asm__ volatile("csrsi mstatus, 8" ::: "memory")
#endif

/*========================================
 *  内部辅助函数
 *========================================*/

/**
 * @brief 计算大于等于 x 的最小 2 的幂
 */
static inline uint32_t next_power_of_two(uint32_t x)
{
    if (x == 0) {
        return 1;
    }
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

/**
 * @brief 计算两个位置之间的数据量（支持环绕）
 */
static inline uint32_t calc_data_len(const fifo_buf_t *fifo, uint32_t head, uint32_t tail)
{
    return (head - tail) & fifo->mask;
}

/**
 * @brief 计算剩余空间
 */
static inline uint32_t calc_free_len(const fifo_buf_t *fifo, uint32_t head, uint32_t tail)
{
    return fifo->size - 1 - calc_data_len(fifo, head, tail);
}

/*========================================
 *  初始化接口实现
 *========================================*/

int fifo_buf_init(fifo_buf_t *fifo, uint8_t *buf, uint32_t size)
{
    if (fifo == NULL || buf == NULL || size == 0) {
        return FIFO_ERR_NULL;
    }

    /* 检查 4 字节对齐 */
    if (((uintptr_t)buf & 0x03) != 0) {
        return FIFO_ERR_ALIGN;
    }

    /* 大小向上取整到 2 的幂 */
    uint32_t actual_size = next_power_of_two(size);
    if (actual_size < 16) {
        actual_size = 16;
    }

    fifo->head = 0;
    fifo->tail = 0;
    fifo->size = actual_size;
    fifo->mask = actual_size - 1;
    fifo->buf = buf;

    return FIFO_OK;
}

int fifo_buf_init_static(fifo_buf_t *fifo, uint32_t size)
{
    if (fifo == NULL || size == 0) {
        return FIFO_ERR_NULL;
    }

    /* 大小向上取整到 2 的幂 */
    uint32_t actual_size = next_power_of_two(size);
    if (actual_size < 16) {
        actual_size = 16;
    }

    /* 检查缓冲区是否足够大 */
    if (fifo->buf == NULL) {
        return FIFO_ERR_NULL;
    }

    fifo->head = 0;
    fifo->tail = 0;
    fifo->size = actual_size;
    fifo->mask = actual_size - 1;

    return FIFO_OK;
}

void fifo_buf_reset(fifo_buf_t *fifo)
{
    if (fifo != NULL) {
        fifo->head = 0;
        fifo->tail = 0;
    }
}

uint32_t fifo_buf_size(const fifo_buf_t *fifo)
{
    if (fifo == NULL) {
        return 0;
    }
    /* 返回实际可用大小（size - 1，因为要区分满和空） */
    return fifo->size - 1;
}

/*========================================
 *  写入接口实现
 *========================================*/

int fifo_buf_write(fifo_buf_t *fifo, const uint8_t *data, uint32_t len)
{
    if (fifo == NULL || data == NULL || len == 0) {
        return FIFO_ERR_NULL;
    }

    uint32_t head = fifo->head;
    uint32_t tail = fifo->tail;
    uint32_t free = calc_free_len(fifo, head, tail);

    /* 覆盖模式：丢弃旧数据 */
#if FIFO_OVERFLOW_MODE == FIFO_OVERFLOW_OVERWRITE
    if (len > free) {
        uint32_t need = len - free;
        fifo->tail = (tail + need) & fifo->mask;
    }
#else
    /* 阻塞模式：缓冲区满返回错误 */
    if (len > free) {
        return FIFO_ERR_FULL;
    }
#endif

    /* 写入数据（处理环绕） */
    uint32_t space_to_end = fifo->size - (head & fifo->mask);
    if (len <= space_to_end) {
        memcpy(&fifo->buf[head & fifo->mask], data, len);
    } else {
        memcpy(&fifo->buf[head & fifo->mask], data, space_to_end);
        memcpy(&fifo->buf[0], &data[space_to_end], len - space_to_end);
    }

    /* 更新写入位置 */
    fifo->head = (head + len) & fifo->mask;

    return FIFO_OK;
}

int fifo_buf_write_isr(fifo_buf_t *fifo, const uint8_t *data, uint32_t len)
{
    ENTER_CRITICAL();
    int ret = fifo_buf_write(fifo, data, len);
    EXIT_CRITICAL();
    return ret;
}

/*========================================
 *  读取接口实现
 *========================================*/

uint8_t *fifo_buf_read(fifo_buf_t *fifo, uint32_t *len)
{
    if (fifo == NULL || len == NULL) {
        return NULL;
    }

    uint32_t head = fifo->head;
    uint32_t tail = fifo->tail;
    uint32_t data_len = calc_data_len(fifo, head, tail);

    if (data_len == 0) {
        *len = 0;
        return NULL;
    }

    /* 返回数据起始地址 */
    uint32_t pos = tail & fifo->mask;
    *len = data_len;

    return &fifo->buf[pos];
}

void fifo_buf_read_done(fifo_buf_t *fifo)
{
    if (fifo != NULL) {
        /* 清空所有数据（简化实现） */
        fifo->tail = fifo->head;
    }
}

uint32_t fifo_buf_read_copy(fifo_buf_t *fifo, uint8_t *data, uint32_t max_len)
{
    if (fifo == NULL || data == NULL || max_len == 0) {
        return 0;
    }

    uint32_t head = fifo->head;
    uint32_t tail = fifo->tail;
    uint32_t data_len = calc_data_len(fifo, head, tail);

    if (data_len == 0) {
        return 0;
    }

    /* 计算实际读取长度 */
    uint32_t to_read = (data_len < max_len) ? data_len : max_len;

    /* 拷贝数据（处理环绕） */
    uint32_t space_to_end = fifo->size - (tail & fifo->mask);
    if (to_read <= space_to_end) {
        memcpy(data, &fifo->buf[tail & fifo->mask], to_read);
    } else {
        memcpy(data, &fifo->buf[tail & fifo->mask], space_to_end);
        memcpy(&data[space_to_end], fifo->buf, to_read - space_to_end);
    }

    /* 更新读取位置 */
    fifo->tail = (tail + to_read) & fifo->mask;

    return to_read;
}

uint8_t *fifo_buf_read_isr(fifo_buf_t *fifo, uint32_t *len)
{
    ENTER_CRITICAL();
    uint8_t *ptr = fifo_buf_read(fifo, len);
    EXIT_CRITICAL();
    return ptr;
}

/*========================================
 *  状态查询接口实现
 *========================================*/

uint32_t fifo_buf_available(const fifo_buf_t *fifo)
{
    if (fifo == NULL) {
        return 0;
    }
    return calc_data_len(fifo, fifo->head, fifo->tail);
}

uint32_t fifo_buf_free(const fifo_buf_t *fifo)
{
    if (fifo == NULL) {
        return 0;
    }
    return calc_free_len(fifo, fifo->head, fifo->tail);
}

bool fifo_buf_is_empty(const fifo_buf_t *fifo)
{
    if (fifo == NULL) {
        return true;
    }
    return fifo->head == fifo->tail;
}

bool fifo_buf_is_full(const fifo_buf_t *fifo)
{
    if (fifo == NULL) {
        return false;
    }
    return calc_free_len(fifo, fifo->head, fifo->tail) == 0;
}

uint8_t *fifo_buf_peek(const fifo_buf_t *fifo, uint32_t *len)
{
    if (fifo == NULL || len == NULL) {
        return NULL;
    }

    uint32_t head = fifo->head;
    uint32_t tail = fifo->tail;
    uint32_t data_len = calc_data_len(fifo, head, tail);

    if (data_len == 0) {
        *len = 0;
        return NULL;
    }

    uint32_t pos = tail & fifo->mask;
    *len = data_len;

    return &fifo->buf[pos];
}
