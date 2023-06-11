#ifndef __EN8811H_H
#define __EN8811H_H

#define EN8811H_MD32_DM             "EthMD32.dm.bin"
#define EN8811H_MD32_DSP            "EthMD32.DSP.bin"

#define EN8811H_PHY_ID1             0x03a2
#define EN8811H_PHY_ID2             0xa411
#define EN8811H_PHY_ID              ((EN8811H_PHY_ID1 << 16) | EN8811H_PHY_ID2)
#define EN8811H_PHY_READY           0x02
#define MAX_RETRY                   5

#define EN8811H_TX_POLARITY_NORMAL   0x1
#define EN8811H_TX_POLARITY_REVERSE  0x0

#define EN8811H_RX_POLARITY_REVERSE  (0x1 << 1)
#define EN8811H_RX_POLARITY_NORMAL   (0x0 << 1)


/*
The following led_cfg example is for reference only.
LED0 Link 2500/Blink 2500 TxRx   (GPIO5)    <-> BASE_T_LED0,
LED1 Link 1000/Blink 1000 TxRx   (GPIO4)    <-> BASE_T_LED1,
LED2 Link 100 /Blink 100  TxRx   (GPIO3)    <-> BASE_T_LED2,
*/
/* User-defined.B */
#define BASE_T_LED0_ON_CFG      (LED_ON_EVT_LINK_2500M)
#define BASE_T_LED0_BLK_CFG     (LED_BLK_EVT_2500M_TX_ACT | LED_BLK_EVT_2500M_RX_ACT)
#define BASE_T_LED1_ON_CFG      (LED_ON_EVT_LINK_1000M)
#define BASE_T_LED1_BLK_CFG     (LED_BLK_EVT_1000M_TX_ACT | LED_BLK_EVT_1000M_RX_ACT)
#define BASE_T_LED2_ON_CFG      (LED_ON_EVT_LINK_100M)
#define BASE_T_LED2_BLK_CFG     (LED_BLK_EVT_100M_TX_ACT | LED_BLK_EVT_100M_RX_ACT)
/* User-defined.E */

/* CL45 MDIO control */
#define MII_MMD_ACC_CTL_REG         0x0d
#define MII_MMD_ADDR_DATA_REG       0x0e
#define MMD_OP_MODE_DATA            BIT(14)

#define EN8811H_FW_VERSION          "1.1.3"

#define LED_ON_CTRL(i)              (0x024 + ((i)*2))
#define LED_ON_EN                   (1 << 15)
#define LED_ON_POL                  (1 << 14)
#define LED_ON_EVT_MASK             (0x1ff)
/* LED ON Event Option.B */
#define LED_ON_EVT_LINK_2500M       (1 << 8)
#define LED_ON_EVT_FORCE            (1 << 6)
#define LED_ON_EVT_LINK_DOWN        (1 << 3)
#define LED_ON_EVT_LINK_100M        (1 << 1)
#define LED_ON_EVT_LINK_1000M       (1 << 0)
/* LED ON Event Option.E */

#define LED_BLK_CTRL(i)             (0x025 + ((i)*2))
#define LED_BLK_EVT_MASK            (0xfff)
/* LED Blinking Event Option.B*/
#define LED_BLK_EVT_2500M_RX_ACT    (1 << 11)
#define LED_BLK_EVT_2500M_TX_ACT    (1 << 10)
#define LED_BLK_EVT_FORCE           (1 << 9)
#define LED_BLK_EVT_100M_RX_ACT     (1 << 3)
#define LED_BLK_EVT_100M_TX_ACT     (1 << 2)
#define LED_BLK_EVT_1000M_RX_ACT    (1 << 1)
#define LED_BLK_EVT_1000M_TX_ACT    (1 << 0)
/* LED Blinking Event Option.E*/
#define LED_ENABLE                  1
#define LED_DISABLE                 0

#define EN8811H_LED_COUNT           3

#define LED_BCR                     (0x021)
#define LED_BCR_EXT_CTRL            (1 << 15)
#define LED_BCR_CLK_EN              (1 << 3)
#define LED_BCR_TIME_TEST           (1 << 2)
#define LED_BCR_MODE_MASK           (3)
#define LED_BCR_MODE_DISABLE        (0)

#define LED_ON_DUR                  (0x022)
#define LED_ON_DUR_MASK             (0xffff)

#define LED_BLK_DUR                 (0x023)
#define LED_BLK_DUR_MASK            (0xffff)

#define UNIT_LED_BLINK_DURATION     1024

#define AIR_RTN_ON_ERR(cond, err)  \
    do { if ((cond)) return (err); } while(0)

#define AIR_RTN_ERR(err)            AIR_RTN_ON_ERR(err < 0, err)

#define LED_SET_EVT(reg, cod, result, bit) do         \
    {                                                 \
        if(reg & cod) {                               \
            result |= bit;                            \
        }                                             \
    } while(0)

#define LED_SET_GPIO_SEL(gpio, led, val) do           \
    {                                                 \
        val |= (led << (8 * (gpio % 4)));         \
    } while(0)

#define INVALID_DATA                0xffff
#define PBUS_INVALID_DATA           0xffffffff

typedef struct AIR_BASE_T_LED_CFG_S
{
    u16 en;
    u16 gpio;
    u16 pol;
    u16 on_cfg;
    u16 blk_cfg;
}AIR_BASE_T_LED_CFG_T;
typedef enum
{
    AIR_LED2_GPIO3 = 3,
    AIR_LED1_GPIO4,
    AIR_LED0_GPIO5,
    AIR_LED_LAST
} AIR_LED_GPIO;

typedef enum {
    AIR_BASE_T_LED0,
    AIR_BASE_T_LED1,
    AIR_BASE_T_LED2,
    AIR_BASE_T_LED3
}AIR_BASE_T_LED;

typedef enum
{
    AIR_LED_BLK_DUR_32M,
    AIR_LED_BLK_DUR_64M,
    AIR_LED_BLK_DUR_128M,
    AIR_LED_BLK_DUR_256M,
    AIR_LED_BLK_DUR_512M,
    AIR_LED_BLK_DUR_1024M,
    AIR_LED_BLK_DUR_LAST
} AIR_LED_BLK_DUT_T;

typedef enum
{
    AIR_ACTIVE_LOW,
    AIR_ACTIVE_HIGH,
} AIR_LED_POLARITY;
typedef enum
{
    AIR_LED_MODE_DISABLE,
    AIR_LED_MODE_USER_DEFINE,
    AIR_LED_MODE_LAST
} AIR_LED_MODE_T;

#endif /* End of __EN8811H_MD32_H */
