#include "App_SHT40.h"
#include "rtc.h"       // 必须包含，才能使用 hrtc 和 BKP 函数
#include <stdio.h>
#include <string.h>

/* DR1 - DR4 已被 RTC 占用 */
/* SHT40 占用 DR5 - DR8 */
#define BKP_DR_TEMP_BOT  RTC_BKP_DR5
#define BKP_DR_TEMP_TOP  RTC_BKP_DR6
#define BKP_DR_HUMI_BOT  RTC_BKP_DR7
#define BKP_DR_HUMI_TOP  RTC_BKP_DR8
#define SHT40_BKP_MAGIC    0xA5A5       // SHT40 专用的魔术数
#define BKP_DR_SHT40_CHK   RTC_BKP_DR9  // 使用 DR9 存储验证位

/*********************************私有辅助函数********************************************/

/**
 * @brief Sensirion 标准 CRC8 校验算法
 * Polynomial: 0x31 (x^8 + x^5 + x^4 + 1)
 * Initialization: 0xFF
 */
static uint8_t SHT40_CalcCRC(uint8_t *data, uint8_t len) {
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x31;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/**
 * @brief 将浮点数阈值保存到 BKP 寄存器
 * 策略: float * 100 -> int16 -> uint32 (存入 BKP)
 */
static void SaveThresholdToBKP(uint32_t BackupRegister, float value) {
    /* 开启 BKP 访问权限 */
    HAL_PWR_EnableBkUpAccess();
    /* 写入数据 */
    HAL_RTCEx_BKUPWrite(&hrtc, BackupRegister, (uint32_t)((int16_t)(value * 100.0f)));
}

/*********************************功能函数实现********************************************/

/**
 * @brief 软复位传感器
 * (注意：为了避免隐式声明警告，建议将此函数放在 Init 之前，或在头文件中声明)
 */
void App_SHT40_SoftReset(SHT40_t *dev) {
    uint8_t cmd = SHT40_CMD_SOFT_RESET;
    HAL_I2C_Master_Transmit(dev->i2cHandle, SHT40_I2C_ADDR, &cmd, 1, 100);
    HAL_Delay(10); // 复位需要时间
}

/**
 * @brief 初始化 SHT40 对象，加载 BKP 数据或设置默认值
 */
void App_SHT40_Init(SHT40_t *dev, I2C_HandleTypeDef *i2cHandle) {
    dev->i2cHandle = i2cHandle;
    
    /* 1. 确保电源和备份域时钟开启 */
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    /* 2. 检查独立的魔术数验证逻辑 */
    if (HAL_RTCEx_BKUPRead(&hrtc, BKP_DR_SHT40_CHK) == SHT40_BKP_MAGIC) {
        
        /* === 情况 A: 已初始化过，从 BKP 加载所有历史数据 === */
        #define READ_BKP_VAL(reg)  ( (int16_t)HAL_RTCEx_BKUPRead(&hrtc, reg) / 100.0f )
        dev->temp_bot = READ_BKP_VAL(BKP_DR_TEMP_BOT);
        dev->temp_top = READ_BKP_VAL(BKP_DR_TEMP_TOP);
        dev->humi_bot = READ_BKP_VAL(BKP_DR_HUMI_BOT);
        dev->humi_top = READ_BKP_VAL(BKP_DR_HUMI_TOP);
    } 
    else {
        /* === 情况 B: 第一次上电（或电池失效），执行“出厂设置”同步 === */
        // 1. 先在内存结构体中设定默认值
        dev->temp_bot = 20.0f;
        dev->temp_top = 30.0f;
        dev->humi_bot = 70.0f; 
        dev->humi_top = 85.0f;
        
        // 2. 立即将这套默认值强制同步到 BKP 寄存器
        // 保证 BKP 里默认值非 0
        SaveThresholdToBKP(BKP_DR_TEMP_BOT, dev->temp_bot);
        SaveThresholdToBKP(BKP_DR_TEMP_TOP, dev->temp_top);
        SaveThresholdToBKP(BKP_DR_HUMI_BOT, dev->humi_bot);
        SaveThresholdToBKP(BKP_DR_HUMI_TOP, dev->humi_top);
        
        // 3. 标记已完成初始化
        HAL_RTCEx_BKUPWrite(&hrtc, BKP_DR_SHT40_CHK, SHT40_BKP_MAGIC);
    }

    /* 4. 执行软复位 */
    App_SHT40_SoftReset(dev); 

		/* 5.参数清洗
     * ===========================================================
     * 安全阈值检查 (防止 Bot >= Top 导致设备同时开启)
     * 强制约束：Bot 必须至少比 Top 小 10% (留出缓冲区)
     * ============================================================ */
    if (dev->temp_bot >= dev->temp_top) {
        dev->temp_bot = dev->temp_top - 5.0f; // 自动修正温度
    }
    // 关键：湿度保护
    // 假设 Top=80, 则 Bot 不能超过 70。防止 Top(80) < Hum(75) < Bot(90) 的情况
    if (dev->humi_bot >= (dev->humi_top - 5.0f)) {
        dev->humi_bot = dev->humi_top - 10.0f; // 强制拉开 10% 的差距
        printf("[App_SHT40 Humi thresholds corrected! Bot:%.1f,Top:%.1f]\r\n", 
               dev->humi_bot, dev->humi_top);
    }

    printf("\r\n[App_SHT40 Init]\r\n");
    printf("[Humi%.1f%%-%.1f%%]\r\n", 
           dev->humi_bot, dev->humi_top);
    printf("[1.set humi_bot | 2.set humi_top]\r\n");
}


/**
 * @brief 读取温湿度 (高精度模式)
 * 包含 CRC 校验，阻塞时间约 10ms
 */
HAL_StatusTypeDef App_SHT40_ReadTempHum(SHT40_t *dev) {
    uint8_t cmd = SHT40_CMD_HIGH_PRECISION;
    uint8_t buffer[6]; // T_MSB, T_LSB, CRC, H_MSB, H_LSB, CRC

    // 1. 发送测量命令
    if (HAL_I2C_Master_Transmit(dev->i2cHandle, SHT40_I2C_ADDR, &cmd, 1, 100) != HAL_OK) {
        return HAL_ERROR;
    }

    // 2. 等待测量完成 (高精度典型值 8.3ms)
    HAL_Delay(10);

    // 3. 读取 6 字节数据
    if (HAL_I2C_Master_Receive(dev->i2cHandle, SHT40_I2C_ADDR, buffer, 6, 100) != HAL_OK) {
        return HAL_ERROR;
    }

    // 4. 数据解析与 CRC 校验
    // --- 温度处理 ---
    uint16_t t_ticks = (buffer[0] << 8) | buffer[1];
    uint8_t t_crc = buffer[2];
    if (SHT40_CalcCRC(buffer, 2) != t_crc) {
        return HAL_ERROR; // 温度 CRC 校验失败
    }
    // --- 湿度处理 ---
    uint16_t h_ticks = (buffer[3] << 8) | buffer[4];
    uint8_t h_crc = buffer[5];
    if (SHT40_CalcCRC(&buffer[3], 2) != h_crc) {
        return HAL_ERROR; // 湿度 CRC 校验失败
    }

    // 5. 物理量转换 (公式来自 SHT40 Datasheet)
    // T = -45 + 175 * raw / (2^16 - 1)
    // RH = -6 + 125 * raw / (2^16 - 1)
    
    dev->temperature = -45.0f + 175.0f * ((float)t_ticks / 65535.0f);
    dev->humidity = -6.0f + 125.0f * ((float)h_ticks / 65535.0f);

    // 6.湿度限幅 (0-100%)
    if (dev->humidity > 100.0f) dev->humidity = 100.0f;
    if (dev->humidity < 0.0f) dev->humidity = 0.0f;

    return HAL_OK;
}

/**
 * @brief 激活 SHT40 内置加热器 (去凝露模式)
 */
HAL_StatusTypeDef App_SHT40_ActivateHeater(SHT40_t *dev) {
    uint8_t cmd[2];
    HAL_StatusTypeDef status;

    /* 组装命令: 200mW 加热 1秒 (0x39, 0xC6) */
    cmd[0] = SHT40_CMD_HEATER_200MW_1S_MSB; 
    cmd[1] = SHT40_CMD_HEATER_200MW_1S_LSB; 

    /* 1. 发送命令 (使用结构体中绑定的 i2cHandle) */
    status = HAL_I2C_Master_Transmit(dev->i2cHandle, SHT40_I2C_ADDR, cmd, 2, 100);
    if (status != HAL_OK) {
        return status; // 发送失败
    }

    /* 2. 必须阻塞等待加热周期完成 
     * 官方手册规定加热时长 1s，我们给 10% 裕量，延时 1100ms
     * 注意：加热期间传感器不响应 I2C 命令
     */
    HAL_Delay(1100); 
    return HAL_OK;
}

/**
 * @brief 解析串口命令修改阈值，并写入 BKP
 * 命令格式: set temp_top 28.5
 */
void App_SHT40_ParseCommand(SHT40_t *dev, char *cmd_line) {
    float val;
    uint8_t update_flag = 0; // 用于标记是否发生了有效的 BKP 写入
    
    if (sscanf(cmd_line, "set temp_bot %f", &val) == 1) {
        //检查：下限必须严格小于当前上限
        if (val >= dev->temp_top) {
            printf("[App_SHT40 temp_bot must < temp_top (Curr: %.2f)]\r\n", dev->temp_top);
            return;
        }
        dev->temp_bot = val;
        SaveThresholdToBKP(BKP_DR_TEMP_BOT, val); // 保存到掉电区
        printf("[App_SHT40 temp_bot set to %.2f℃]\r\n", val);
        update_flag = 1; // 标记已更新
    } 
    else if (sscanf(cmd_line, "set temp_top %f", &val) == 1) {
        // 检查：上限必须严格大于当前下限
        if (val <= dev->temp_bot) {
            printf("[App_SHT40 temp_top must be > temp_bot (Curr: %.2f)]\r\n", dev->temp_bot);
            return;
        }
        dev->temp_top = val;
        SaveThresholdToBKP(BKP_DR_TEMP_TOP, val); // 保存到掉电区
        printf("[App_SHT40 temp_top set to %.2f℃]\r\n", val);
        update_flag = 1; // 标记已更新
    }
    else if (sscanf(cmd_line, "set humi_bot %f", &val) == 1) {
        /* 检查：湿度下限必须严格小于当前上限 */
        if (val >= dev->humi_top) {
            printf("[App_SHT40 humi_bot must be < humi_top (Curr: %.2f)]\r\n", dev->humi_top);
            return;
        }
        dev->humi_bot = val;
        SaveThresholdToBKP(BKP_DR_HUMI_BOT, val); // 保存到掉电区
        printf("[App_SHT40 humi_bot set to %.2f%%]\r\n", val);
        update_flag = 1; // 标记已更新
    }
    else if (sscanf(cmd_line, "set humi_top %f", &val) == 1) {
        /* 检查：湿度上限必须严格大于当前下限 */
        if (val <= dev->humi_bot) {
            printf("[App_SHT40 humi_top must be > humi_bot (Curr: %.2f)]\r\n", dev->humi_bot);
            return;
        }
        dev->humi_top = val;
        SaveThresholdToBKP(BKP_DR_HUMI_TOP, val); // 保存到掉电区
        printf("[App_SHT40 humi_top set to %.2f%%]\r\n", val);
        update_flag = 1; // 标记已更新
    }

    /* 如果发生了任何阈值更新，写入独立魔术数标记已初始化 */
    if (update_flag) {
        HAL_PWR_EnableBkUpAccess(); // 开启备份域访问
        HAL_RTCEx_BKUPWrite(&hrtc, BKP_DR_SHT40_CHK, SHT40_BKP_MAGIC); // 写入魔术数
    }
}


/**
 * @brief  封装函数：读取并打印温湿度
 * @param  dev: 传感器结构体指针
 */
void App_SHT40_Print(SHT40_t *dev) {
    // 1. 读取温湿度
    // 注意：这里只传 dev，不要写 SHT40_t *
    HAL_StatusTypeDef status = App_SHT40_ReadTempHum(dev);

    // 2. 判断读取是否成功
    if (status == HAL_OK) {
        // 读取成功，打印数值
        printf("Temp:%.2f℃  Humi:%.2f%%\r\n", dev->temperature, dev->humidity);
    } else {
        // 读取失败，打印错误提示
        printf("[App_SHT40 Read Error]\r\n");
    }
}

/**
 * @brief 周期性读取并打印
 * @note  需要在 main 的 while(1) 中调用
 */
void App_SHT40_NEWS(SHT40_t *dev, uint32_t interval_ms) {
    /* 静态变量保存上次执行的时间戳 (static 保证函数退出后数值不丢失) */
    static uint32_t news_tick = 0;

    /* 非阻塞时间差检查 */
    if (HAL_GetTick() - news_tick >= interval_ms) {
        // 1. 更新时间戳
        news_tick = HAL_GetTick();
        // 2. 执行读取
        if (App_SHT40_ReadTempHum(dev) == HAL_OK) {
        // 3. 打印数据
            printf("[App_SHT40 Temp: %.2f C, Humi: %.2f %%]\r\n", 
                   dev->temperature, dev->humidity);
        } else {
            //读取失败提示
            printf("[App_SHT40 Read Failed]\r\n");
        }
    }
}
