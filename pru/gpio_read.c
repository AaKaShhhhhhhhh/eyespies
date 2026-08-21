/*
 * gpio_read.c
 * ---------------------------------------------------------------------------
 * Read the ACTUAL GPIO0 DATAOUT register straight from the hardware via
 * /dev/mem (mmap). Unlike `gpioget`, this does NOT request the line, so it
 * works while `gpio_hold` (or anything else) already owns the pin -- no
 * "Device or resource busy".
 *
 * Use it together with the PRU HOLD_HIGH firmware to answer ONE question:
 *   "Did the PRU's write to the GPIO register actually land?"
 *
 *   sudo ./gpio_hold &          # keep the pin muxed/clocked/alive
 *   sudo ./load_pru0.sh hold_high.out
 *   sudo ./gpio_read            # prints GPIO0_19 (DATAOUT bit 19)
 *
 *   -> bit 19 = 1 : the PRU's OCP write REACHED the GPIO block (bus works)
 *   -> bit 19 = 0 : the PRU's write is NOT reaching the register (bus/mux bug)
 *
 * Build:  gcc -O2 -Wall -o gpio_read gpio_read.c
 * ---------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define GPIO0_BASE   0x44E07000u
#define GPIO_DATAOUT 0x13Cu   /* what the PRU writes with SETDATA/CLEARDATA */
#define GPIO_DATAIN  0x138u   /* pad level (only meaningful if OE=input)    */
#define GPIO_OE      0x134u
#define SERVO_BIT    (1u << 19)

int main(void)
{
    int fd = open("/dev/mem", O_RDONLY);
    if (fd < 0) {
        perror("open /dev/mem");
        fprintf(stderr, "Kernel may block /dev/mem (CONFIG_STRICT_DEVMEM). "
                        "If so, Ctrl-C gpio_hold and use: gpioget gpiochip0 19\n");
        return 1;
    }

    void *map = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, GPIO0_BASE);
    if (map == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    volatile uint32_t *reg = (volatile uint32_t *)map;
    uint32_t dataout = reg[GPIO_DATAOUT / 4];
    uint32_t datain  = reg[GPIO_DATAIN  / 4];
    uint32_t oe      = reg[GPIO_OE      / 4];

    printf("GPIO0_OE      = 0x%08X  (bit19=%u -> 0=output,1=input)\n",
           oe, (oe >> 19) & 1u);
    printf("GPIO0_DATAOUT = 0x%08X  (bit19=%u  <- PRU writes here)\n",
           dataout, (dataout >> 19) & 1u);
    printf("GPIO0_DATAIN  = 0x%08X  (bit19=%u  <- pad level if input)\n",
           datain, (datain >> 19) & 1u);
    printf("----------------------------------------------------------\n");
    printf("GPIO0_19 (SERVO) DATAOUT bit = %u  => %s\n",
           (dataout >> 19) & 1u,
           ((dataout >> 19) & 1u) ? "PRU REACHED THE REGISTER (HIGH)"
                                  : "PRU WRITE NOT SEEN (LOW)");

    munmap(map, 0x1000);
    close(fd);
    return 0;
}
