# Minimal RCC for STM32G4 using the single `request` object style.
# Enough for CubeG4 SystemClock_Config / HAL_RCC_OscConfig / HAL_RCC_ClockConfig.

# ---- Offsets (subset) ----
CR        = 0x00
ICSCR     = 0x04
CFGR      = 0x08
PLLCFGR   = 0x0C
CIER      = 0x18
CIFR      = 0x1C
CICR      = 0x20
AHB1ENR   = 0x48
AHB2ENR   = 0x4C
AHB3ENR   = 0x50
APB1ENR1  = 0x58
APB1ENR2  = 0x5C
APB2ENR   = 0x60
CCIPR     = 0x88
BDCR      = 0x90
CSR       = 0x94
CRRCR     = 0x98     # HSI48
CCIPR2    = 0xA8     # if present

# ---- Bits we care about ----
# CR
HSION   = 1 << 8
HSIRDY  = 1 << 10
HSEON   = 1 << 16
HSERDY  = 1 << 17
PLLON   = 1 << 24
PLLRDY  = 1 << 25
# CFGR
SW_MASK = 0b11          # 0=HSI, 1=HSE, 2=PLL, 3=MSI (present on G4)
SWS_POS = 2
# CRRCR
HSI48ON  = 1 << 0
HSI48RDY = 1 << 1

# ---- State (register mirrors) ----
try:
    _rcc          # if already defined, keep state across calls
except NameError:
    _rcc = {}

def _R(off):  return _rcc.get(off, 0) & 0xFFFFFFFF
def _W(off,v): _rcc[off] = v & 0xFFFFFFFF
def _SET(off,mask,cond):
    v = _R(off)
    v = (v | mask) if cond else (v & ~mask)
    _W(off, v)

def _apply_sws_from_sw():
    cfgr = _R(CFGR)
    sw   = (cfgr & SW_MASK)
    cr   = _R(CR)

    # Only switch SWS if the source is "ready"
    if sw == 0 and (cr & HSIRDY):
        sws = 0
    elif sw == 1 and (cr & HSERDY):
        sws = 1
    elif sw == 2 and (cr & PLLRDY):
        sws = 2
    elif sw == 3:
        sws = 3
    else:
        # keep previous SWS
        sws = (cfgr >> SWS_POS) & 0x3

    cfgr = (cfgr & ~(0x3 << SWS_POS)) | (sws << SWS_POS)
    _W(CFGR, cfgr)

if request.isInit:
    # Reset-like defaults: HSI on & ready, rest off; SWS = HSI
    _W(CR, 0); _W(ICSCR, 0); _W(CFGR, 0); _W(PLLCFGR, 0)
    for o in (CIER, CIFR, CICR, AHB1ENR, AHB2ENR, AHB3ENR, APB1ENR1, APB1ENR2,
              APB2ENR, CCIPR, BDCR, CSR, CRRCR, CCIPR2):
        _W(o, 0)

    _SET(CR, HSION, True)
    _SET(CR, HSIRDY, True)
    _apply_sws_from_sw()
    self.NoisyLog("RCC: init done (HSI ready, SWS=HSI)")

else:
    off = request.offset

    # ---- Reads ----
    if request.isRead:
        self.NoisyLog("RCC: read offset 0x%x" % off)
        request.value = _R(off)

    # ---- Writes ----
    elif request.isWrite:
        val = request.value & 0xFFFFFFFF
        self.NoisyLog("RCC: write offset 0x%x value 0x%x" % (off, val))
        
        if off == CR:
            _W(CR, val)
            # Mark RDY flags immediately when ON bits set
            _SET(CR, HSIRDY, bool(val & HSION))
            _SET(CR, HSERDY, bool(val & HSEON))
            _SET(CR, PLLRDY, bool(val & PLLON))

        elif off == CFGR:
            _W(CFGR, val)
            _apply_sws_from_sw()

        elif off == CRRCR:
            _W(CRRCR, val)
            _SET(CRRCR, HSI48RDY, bool(val & HSI48ON))

        elif off in (PLLCFGR, CIER, CIFR, CICR, BDCR, CSR, CCIPR, CCIPR2,
                     AHB1ENR, AHB2ENR, AHB3ENR, APB1ENR1, APB1ENR2, APB2ENR):
            _W(off, val)

        else:
            _W(off, val)

    # (Optional) verbose trace
    # self.NoisyLog("RCC access: %s off=0x%x val=0x%x" %
    #               ("R" if request.isRead else "W", request.offset,
    #                request.value if not request.isRead else request.value))