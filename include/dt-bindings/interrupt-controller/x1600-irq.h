#ifndef _DT_BINDINGS_INTERRUPT_CONTROLLER_X1600_IRQ_H
#define _DT_BINDINGS_INTERRUPT_CONTROLLER_X1600_IRQ_H

#include <dt-bindings/interrupt-controller/mips-irq.h>

#define IRQ_AUDIO			(0)
#define IRQ_OTG				(1)
#define IRQ_RESERVED0_2			(2)
#define IRQ_PDMA			(3)
#define IRQ_PDMAD			(4)
#define IRQ_PDMAM			(5)
#define IRQ_PWM				(6)
#define IRQ_SFC				(7)
#define IRQ_RESERVED0_8			(8)
#define IRQ_SSI0			(9)
#define IRQ_RESERVED0_10		(10)
#define IRQ_SADC			(11)
#define IRQ_RESERVED0_12		(12)
#define IRQ_RESERVED0_13		(13)
#define IRQ_GPIO3			(14)
#define IRQ_GPIO2			(15)
#define IRQ_GPIO1			(16)
#define IRQ_GPIO0			(17)
#define IRQ_RESERVED0_18		(18)
#define IRQ_RESERVED0_19		(19)
#define IRQ_RESERVED0_20		(20)
#define IRQ_RESERVED0_21		(21)
#define IRQ_HASH			(22)
#define IRQ_AES				(23)
#define IRQ_RESERVED0_14		(24)
#define IRQ_TCU2			(25)
#define IRQ_TCU1			(26)
#define IRQ_TCU0			(27)
#define IRQ_MIPI_CSI_2			(28)
#define IRQ_RESERVED0_29		(29)
#define IRQ_CIM				(30)
#define IRQ_LCD				(31)

#define IRQ_RTC				(32 + 0)
#define IRQ_SOFT			(32 + 1)
#define IRQ_DTRNG			(32 + 2)
#define IRQ_RESERVED1_3			(32 + 3)
#define IRQ_MSC1			(32 + 4)
#define IRQ_MSC0			(32 + 5)
#define IRQ_RESERVED1_6			(32 + 6)
#define IRQ_RESERVED1_7			(32 + 7)
#define IRQ_CAN0			(32 + 8)
#define IRQ_CAN1			(32 + 9)
#define IRQ_CDBUS			(32 + 10)
#define IRQ_RESERVED1_11		(32 + 11)
#define IRQ_UART3			(32 + 12)
#define IRQ_UART2			(32 + 13)
#define IRQ_UART1			(32 + 14)
#define IRQ_UART0			(32 + 15)
#define IRQ_RESERVED1_16		(32 + 16)
#define IRQ_HARB2			(32 + 17)
#define IRQ_HARB0			(32 + 18)
#define IRQ_CPM				(32 + 19)
#define IRQ_DDR				(32 + 20)
#define IRQ_RESERVED1_21		(32 + 21)
#define IRQ_EFUSE			(32 + 22)
#define IRQ_GMAC0			(32 + 23)
#define IRQ_RESERVED1_24		(32 + 24)
#define IRQ_RESERVED1_25		(32 + 25)
#define IRQ_RESERVED1_26		(32 + 26)
#define IRQ_RESERVED1_27		(32 + 27)
#define IRQ_I2C1			(32 + 28)
#define IRQ_I2C0			(32 + 29)
#define IRQ_SSI_SLV			(32 + 30)
#define IRQ_RESERVED1_31		(32 + 31)

#endif
