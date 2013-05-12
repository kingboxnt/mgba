#include "isa-arm.h"

#include "arm.h"
#include "isa-inlines.h"

enum {
	PSR_USER_MASK = 0xF0000000,
	PSR_PRIV_MASK = 0x000000CF,
	PSR_STATE_MASK = 0x00000020
};

#define ARM_PREFETCH_CYCLES (1 + cpu->memory->activePrefetchCycles32)

// Addressing mode 1
static inline void _shiftLSL(struct ARMCore* cpu, uint32_t opcode) {
	int rm = opcode & 0x0000000F;
	int immediate = (opcode & 0x00000F80) >> 7;
	if (!immediate) {
		cpu->shifterOperand = cpu->gprs[rm];
		cpu->shifterCarryOut = cpu->cpsr.c;
	} else {
		cpu->shifterOperand = cpu->gprs[rm] << immediate;
		cpu->shifterCarryOut = (cpu->gprs[rm] >> (32 - immediate)) & 1;
	}
}

static inline void _shiftLSLR(struct ARMCore* cpu, uint32_t opcode) {
	int rm = opcode & 0x0000000F;
	int rs = (opcode >> 8) & 0x0000000F;
	++cpu->cycles;
	int shift = cpu->gprs[rs];
	if (rs == ARM_PC) {
		shift += 4;
	}
	shift &= 0xFF;
	int32_t shiftVal = cpu->gprs[rm];
	if (rm == ARM_PC) {
		shiftVal += 4;
	}
	if (!shift) {
		cpu->shifterOperand = shiftVal;
		cpu->shifterCarryOut = cpu->cpsr.c;
	} else if (shift < 32) {
		cpu->shifterOperand = shiftVal << shift;
		cpu->shifterCarryOut = (shiftVal >> (32 - shift)) & 1;
	} else if (shift == 32) {
		cpu->shifterOperand = 0;
		cpu->shifterCarryOut = shiftVal & 1;
	} else {
		cpu->shifterOperand = 0;
		cpu->shifterCarryOut = 0;
	}
}

static inline void _shiftLSR(struct ARMCore* cpu, uint32_t opcode) {
	int rm = opcode & 0x0000000F;
	int immediate = (opcode & 0x00000F80) >> 7;
	if (immediate) {
		cpu->shifterOperand = ((uint32_t) cpu->gprs[rm]) >> immediate;
		cpu->shifterCarryOut = (cpu->gprs[rm] >> (immediate - 1)) & 1;
	} else {
		cpu->shifterOperand = 0;
		cpu->shifterCarryOut = ARM_SIGN(cpu->gprs[rm]);
	}
}

static inline void _shiftLSRR(struct ARMCore* cpu, uint32_t opcode) {
	int rm = opcode & 0x0000000F;
	int rs = (opcode >> 8) & 0x0000000F;
	++cpu->cycles;
	int shift = cpu->gprs[rs];
	if (rs == ARM_PC) {
		shift += 4;
	}
	shift &= 0xFF;
	uint32_t shiftVal = cpu->gprs[rm];
	if (rm == ARM_PC) {
		shiftVal += 4;
	}
	if (!shift) {
		cpu->shifterOperand = shiftVal;
		cpu->shifterCarryOut = cpu->cpsr.c;
	} else if (shift < 32) {
		cpu->shifterOperand = shiftVal >> shift;
		cpu->shifterCarryOut = (shiftVal >> (shift - 1)) & 1;
	} else if (shift == 32) {
		cpu->shifterOperand = 0;
		cpu->shifterCarryOut = shiftVal >> 31;
	} else {
		cpu->shifterOperand = 0;
		cpu->shifterCarryOut = 0;
	}
}

static inline void _shiftASR(struct ARMCore* cpu, uint32_t opcode) {
	int rm = opcode & 0x0000000F;
	int immediate = (opcode & 0x00000F80) >> 7;
	if (immediate) {
		cpu->shifterOperand = cpu->gprs[rm] >> immediate;
		cpu->shifterCarryOut = (cpu->gprs[rm] >> (immediate - 1)) & 1;
	} else {
		cpu->shifterCarryOut = ARM_SIGN(cpu->gprs[rm]);
		cpu->shifterOperand = cpu->shifterCarryOut;
	}
}

static inline void _shiftASRR(struct ARMCore* cpu, uint32_t opcode) {
	int rm = opcode & 0x0000000F;
	int rs = (opcode >> 8) & 0x0000000F;
	++cpu->cycles;
	int shift = cpu->gprs[rs];
	if (rs == ARM_PC) {
		shift += 4;
	}
	shift &= 0xFF;
	int shiftVal =  cpu->gprs[rm];
	if (rm == ARM_PC) {
		shiftVal += 4;
	}
	if (!shift) {
		cpu->shifterOperand = shiftVal;
		cpu->shifterCarryOut = cpu->cpsr.c;
	} else if (shift < 32) {
		cpu->shifterOperand = shiftVal >> shift;
		cpu->shifterCarryOut = (shiftVal >> (shift - 1)) & 1;
	} else if (cpu->gprs[rm] >> 31) {
		cpu->shifterOperand = 0xFFFFFFFF;
		cpu->shifterCarryOut = 1;
	} else {
		cpu->shifterOperand = 0;
		cpu->shifterCarryOut = 0;
	}
}

static inline void _shiftROR(struct ARMCore* cpu, uint32_t opcode) {
	int rm = opcode & 0x0000000F;
	int immediate = (opcode & 0x00000F80) >> 7;
	if (immediate) {
		cpu->shifterOperand = ARM_ROR(cpu->gprs[rm], immediate);
		cpu->shifterCarryOut = (cpu->gprs[rm] >> (immediate - 1)) & 1;
	} else {
		// RRX
		cpu->shifterOperand = (cpu->cpsr.c << 31) | (((uint32_t) cpu->gprs[rm]) >> 1);
		cpu->shifterCarryOut = cpu->gprs[rm] & 0x00000001;
	}
}

static inline void _shiftRORR(struct ARMCore* cpu, uint32_t opcode) {
	int rm = opcode & 0x0000000F;
	int rs = (opcode >> 8) & 0x0000000F;
	++cpu->cycles;
	int shift = cpu->gprs[rs];
	if (rs == ARM_PC) {
		shift += 4;
	}
	shift &= 0xFF;
	int shiftVal =  cpu->gprs[rm];
	if (rm == ARM_PC) {
		shiftVal += 4;
	}
	int rotate = shift & 0x1F;
	if (!shift) {
		cpu->shifterOperand = shiftVal;
		cpu->shifterCarryOut = cpu->cpsr.c;
	} else if (rotate) {
		cpu->shifterOperand = ARM_ROR(shiftVal, rotate);
		cpu->shifterCarryOut = (shiftVal >> (rotate - 1)) & 1;
	} else {
		cpu->shifterOperand = shiftVal;
		cpu->shifterCarryOut = ARM_SIGN(shiftVal);
	}
}

static inline void _immediate(struct ARMCore* cpu, uint32_t opcode) {
	int rotate = (opcode & 0x00000F00) >> 7;
	int immediate = opcode & 0x000000FF;
	if (!rotate) {
		cpu->shifterOperand = immediate;
		cpu->shifterCarryOut = cpu->cpsr.c;
	} else {
		cpu->shifterOperand = ARM_ROR(immediate, rotate);
		cpu->shifterCarryOut = ARM_SIGN(cpu->shifterOperand);
	}
}

static const ARMInstruction _armTable[0x1000];

static ARMInstruction _ARMLoadInstructionARM(struct ARMMemory* memory, uint32_t address, uint32_t* opcodeOut) {
	uint32_t opcode = memory->activeRegion[(address & memory->activeMask) >> 2];
	*opcodeOut = opcode;
	return _armTable[((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0x00F)];
}

void ARMStep(struct ARMCore* cpu) {
	// TODO
	uint32_t opcode;
	ARMInstruction instruction = _ARMLoadInstructionARM(cpu->memory, cpu->gprs[ARM_PC] - WORD_SIZE_ARM, &opcode);
	cpu->gprs[ARM_PC] += WORD_SIZE_ARM;

	int condition = opcode >> 28;
	if (condition == 0xE) {
		instruction(cpu, opcode);
		return;
	} else {
		switch (condition) {
		case 0x0:
			if (!ARM_COND_EQ) {
				cpu->cycles += ARM_PREFETCH_CYCLES;
				return;
			}
			break;
		case 0x1:
			if (!ARM_COND_NE) {
				cpu->cycles += ARM_PREFETCH_CYCLES;
				return;
			}
			break;
		case 0x2:
			if (!ARM_COND_CS) {
				cpu->cycles += ARM_PREFETCH_CYCLES;
				return;
			}
			break;
		case 0x3:
			if (!ARM_COND_CC) {
				cpu->cycles += ARM_PREFETCH_CYCLES;
				return;
			}
			break;
		case 0x4:
			if (!ARM_COND_MI) {
				cpu->cycles += ARM_PREFETCH_CYCLES;
				return;
			}
			break;
		case 0x5:
			if (!ARM_COND_PL) {
				cpu->cycles += ARM_PREFETCH_CYCLES;
				return;
			}
			break;
		case 0x6:
			if (!ARM_COND_VS) {
				cpu->cycles += ARM_PREFETCH_CYCLES;
				return;
			}
			break;
		case 0x7:
			if (!ARM_COND_VC) {
				cpu->cycles += ARM_PREFETCH_CYCLES;
				return;
			}
			break;
		case 0x8:
			if (!ARM_COND_HI) {
				cpu->cycles += ARM_PREFETCH_CYCLES;
				return;
			}
			break;
		case 0x9:
			if (!ARM_COND_LS) {
				cpu->cycles += ARM_PREFETCH_CYCLES;
				return;
			}
			break;
		case 0xA:
			if (!ARM_COND_GE) {
				cpu->cycles += ARM_PREFETCH_CYCLES;
				return;
			}
			break;
		case 0xB:
			if (!ARM_COND_LT) {
				cpu->cycles += ARM_PREFETCH_CYCLES;
				return;
			}
			break;
		case 0xC:
			if (!ARM_COND_GT) {
				cpu->cycles += ARM_PREFETCH_CYCLES;
				return;
			}
			break;
		case 0xD:
			if (!ARM_COND_LE) {
				cpu->cycles += ARM_PREFETCH_CYCLES;
				return;
			}
			break;
		default:
			break;
		}
	}
	instruction(cpu, opcode);
}

// Instruction definitions
// Beware pre-processor antics

#define ARM_ADDITION_S(M, N, D) \
	if (rd == ARM_PC && _ARMModeHasSPSR(cpu->cpsr.priv)) { \
		cpu->cpsr = cpu->spsr; \
		_ARMReadCPSR(cpu); \
	} else { \
		cpu->cpsr.n = ARM_SIGN(D); \
		cpu->cpsr.z = !(D); \
		cpu->cpsr.c = ARM_CARRY_FROM(M, N, D); \
		cpu->cpsr.v = ARM_V_ADDITION(M, N, D); \
	}

#define ARM_SUBTRACTION_S(M, N, D) \
	if (rd == ARM_PC && _ARMModeHasSPSR(cpu->cpsr.priv)) { \
		cpu->cpsr = cpu->spsr; \
		_ARMReadCPSR(cpu); \
	} else { \
		cpu->cpsr.n = ARM_SIGN(D); \
		cpu->cpsr.z = !(D); \
		cpu->cpsr.c = ARM_BORROW_FROM(M, N, D); \
		cpu->cpsr.v = ARM_V_SUBTRACTION(M, N, D); \
	}

#define ARM_NEUTRAL_S(M, N, D) \
	if (rd == ARM_PC && _ARMModeHasSPSR(cpu->cpsr.priv)) { \
		cpu->cpsr = cpu->spsr; \
		_ARMReadCPSR(cpu); \
	} else { \
		cpu->cpsr.n = ARM_SIGN(D); \
		cpu->cpsr.z = !(D); \
		cpu->cpsr.c = cpu->shifterCarryOut; \
	}

#define ARM_NEUTRAL_HI_S(DLO, DHI) \
	cpu->cpsr.n = ARM_SIGN(DHI); \
	cpu->cpsr.z = !((DHI) | (DLO));

#define ADDR_MODE_2_I_TEST (opcode & 0x00000F80)
#define ADDR_MODE_2_I ((opcode & 0x00000F80) >> 7)
#define ADDR_MODE_2_ADDRESS (address)
#define ADDR_MODE_2_RN (cpu->gprs[rn])
#define ADDR_MODE_2_RM (cpu->gprs[rm])
#define ADDR_MODE_2_IMMEDIATE (opcode & 0x00000FFF)
#define ADDR_MODE_2_INDEX(U_OP, M) (cpu->gprs[rn] U_OP M)
#define ADDR_MODE_2_WRITEBACK(ADDR) (cpu->gprs[rn] = ADDR)
#define ADDR_MODE_2_LSL (cpu->gprs[rm] << ADDR_MODE_2_I)
#define ADDR_MODE_2_LSR (ADDR_MODE_2_I_TEST ? ((uint32_t) cpu->gprs[rm]) >> ADDR_MODE_2_I : 0)
#define ADDR_MODE_2_ASR (ADDR_MODE_2_I_TEST ? ((int32_t) cpu->gprs[rm]) >> ADDR_MODE_2_I : ((int32_t) cpu->gprs[rm]) >> 31)
#define ADDR_MODE_2_ROR (ADDR_MODE_2_I_TEST ? ARM_ROR(cpu->gprs[rm], ADDR_MODE_2_I) : (cpu->cpsr.c << 31) | (((uint32_t) cpu->gprs[rm]) >> 1))

#define ADDR_MODE_3_ADDRESS ADDR_MODE_2_ADDRESS
#define ADDR_MODE_3_RN ADDR_MODE_2_RN
#define ADDR_MODE_3_RM ADDR_MODE_2_RM
#define ADDR_MODE_3_IMMEDIATE (((opcode & 0x00000F00) >> 4) | (opcode & 0x0000000F))
#define ADDR_MODE_3_INDEX(U_OP, M) ADDR_MODE_2_INDEX(U_OP, M)
#define ADDR_MODE_3_WRITEBACK(ADDR) ADDR_MODE_2_WRITEBACK(ADDR)

#define ARM_LOAD_POST_BODY \
	if (rd == ARM_PC) { \
		ARM_WRITE_PC; \
	}

#define ARM_STORE_POST_BODY \
	currentCycles -= ARM_PREFETCH_CYCLES; \
	currentCycles += 1 + cpu->memory->activeNonseqCycles32;

#define DEFINE_INSTRUCTION_ARM(NAME, BODY) \
	static void _ARMInstruction ## NAME (struct ARMCore* cpu, uint32_t opcode) { \
		int currentCycles = ARM_PREFETCH_CYCLES; \
		BODY; \
		cpu->cycles += currentCycles; \
	}

#define DEFINE_ALU_INSTRUCTION_EX_ARM(NAME, S_BODY, SHIFTER, BODY) \
	DEFINE_INSTRUCTION_ARM(NAME, \
		int rd = (opcode >> 12) & 0xF; \
		int rn = (opcode >> 16) & 0xF; \
		UNUSED(rn); \
		SHIFTER(cpu, opcode); \
		BODY; \
		S_BODY; \
		if (rd == ARM_PC) { \
			if (cpu->executionMode == MODE_ARM) { \
				ARM_WRITE_PC; \
			} else { \
				THUMB_WRITE_PC; \
			} \
		})

#define DEFINE_ALU_INSTRUCTION_ARM(NAME, S_BODY, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## _LSL, , _shiftLSL, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## S_LSL, S_BODY, _shiftLSL, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## _LSLR, , _shiftLSLR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## S_LSLR, S_BODY, _shiftLSLR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## _LSR, , _shiftLSR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## S_LSR, S_BODY, _shiftLSR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## _LSRR, , _shiftLSRR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## S_LSRR, S_BODY, _shiftLSRR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## _ASR, , _shiftASR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## S_ASR, S_BODY, _shiftASR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## _ASRR, , _shiftASRR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## S_ASRR, S_BODY, _shiftASRR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## _ROR, , _shiftROR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## S_ROR, S_BODY, _shiftROR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## _RORR, , _shiftRORR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## S_RORR, S_BODY, _shiftRORR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## I, , _immediate, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## SI, S_BODY, _immediate, BODY)

#define DEFINE_ALU_INSTRUCTION_S_ONLY_ARM(NAME, S_BODY, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## _LSL, S_BODY, _shiftLSL, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## _LSLR, S_BODY, _shiftLSLR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## _LSR, S_BODY, _shiftLSR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## _LSRR, S_BODY, _shiftLSRR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## _ASR, S_BODY, _shiftASR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## _ASRR, S_BODY, _shiftASRR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## _ROR, S_BODY, _shiftROR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## _RORR, S_BODY, _shiftRORR, BODY) \
	DEFINE_ALU_INSTRUCTION_EX_ARM(NAME ## I, S_BODY, _immediate, BODY)

#define DEFINE_MULTIPLY_INSTRUCTION_EX_ARM(NAME, BODY, S_BODY) \
	DEFINE_INSTRUCTION_ARM(NAME, \
		int rd = (opcode >> 12) & 0xF; \
		int rdHi = (opcode >> 16) & 0xF; \
		int rs = (opcode >> 8) & 0xF; \
		int rm = opcode & 0xF; \
		UNUSED(rdHi); \
		ARM_WAIT_MUL(cpu->gprs[rs]); \
		BODY; \
		S_BODY; \
		if (rd == ARM_PC) { \
			ARM_WRITE_PC; \
		})

#define DEFINE_MULTIPLY_INSTRUCTION_ARM(NAME, BODY, S_BODY) \
	DEFINE_MULTIPLY_INSTRUCTION_EX_ARM(NAME, BODY, ) \
	DEFINE_MULTIPLY_INSTRUCTION_EX_ARM(NAME ## S, BODY, S_BODY)

#define DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME, ADDRESS, WRITEBACK, BODY) \
	DEFINE_INSTRUCTION_ARM(NAME, \
		uint32_t address; \
		int rn = (opcode >> 16) & 0xF; \
		int rd = (opcode >> 12) & 0xF; \
		int rm = opcode & 0xF; \
		UNUSED(rm); \
		address = ADDRESS; \
		BODY; \
		WRITEBACK;)

#define DEFINE_LOAD_STORE_INSTRUCTION_SHIFTER_ARM(NAME, SHIFTER, BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME, ADDR_MODE_2_RN, ADDR_MODE_2_WRITEBACK(ADDR_MODE_2_INDEX(-, SHIFTER)), BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## U, ADDR_MODE_2_RN, ADDR_MODE_2_WRITEBACK(ADDR_MODE_2_INDEX(+, SHIFTER)), BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## P, ADDR_MODE_2_INDEX(-, SHIFTER), , BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## PW, ADDR_MODE_2_INDEX(-, SHIFTER), ADDR_MODE_2_WRITEBACK(ADDR_MODE_2_ADDRESS), BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## PU, ADDR_MODE_2_INDEX(+, SHIFTER), , BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## PUW, ADDR_MODE_2_INDEX(+, SHIFTER), ADDR_MODE_2_WRITEBACK(ADDR_MODE_2_ADDRESS), BODY)

#define DEFINE_LOAD_STORE_INSTRUCTION_ARM(NAME, BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_SHIFTER_ARM(NAME ## _LSL_, ADDR_MODE_2_LSL, BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_SHIFTER_ARM(NAME ## _LSR_, ADDR_MODE_2_LSR, BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_SHIFTER_ARM(NAME ## _ASR_, ADDR_MODE_2_ASR, BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_SHIFTER_ARM(NAME ## _ROR_, ADDR_MODE_2_ROR, BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## I, ADDR_MODE_2_RN, ADDR_MODE_2_WRITEBACK(ADDR_MODE_2_INDEX(-, ADDR_MODE_2_IMMEDIATE)), BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## IU, ADDR_MODE_2_RN, ADDR_MODE_2_WRITEBACK(ADDR_MODE_2_INDEX(+, ADDR_MODE_2_IMMEDIATE)), BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## IP, ADDR_MODE_2_INDEX(-, ADDR_MODE_2_IMMEDIATE), , BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## IPW, ADDR_MODE_2_INDEX(-, ADDR_MODE_2_IMMEDIATE), ADDR_MODE_2_WRITEBACK(ADDR_MODE_2_ADDRESS), BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## IPU, ADDR_MODE_2_INDEX(+, ADDR_MODE_2_IMMEDIATE), , BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## IPUW, ADDR_MODE_2_INDEX(+, ADDR_MODE_2_IMMEDIATE), ADDR_MODE_2_WRITEBACK(ADDR_MODE_2_ADDRESS), BODY) \

#define DEFINE_LOAD_STORE_MODE_3_INSTRUCTION_ARM(NAME, BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME, ADDR_MODE_3_RN, ADDR_MODE_3_WRITEBACK(ADDR_MODE_3_INDEX(-, ADDR_MODE_3_RM)), BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## U, ADDR_MODE_3_RN, ADDR_MODE_3_WRITEBACK(ADDR_MODE_3_INDEX(+, ADDR_MODE_3_RM)), BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## P, ADDR_MODE_3_INDEX(-, ADDR_MODE_3_RM), , BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## PW, ADDR_MODE_3_INDEX(-, ADDR_MODE_3_RM), ADDR_MODE_3_WRITEBACK(ADDR_MODE_3_ADDRESS), BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## PU, ADDR_MODE_3_INDEX(+, ADDR_MODE_3_RM), , BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## PUW, ADDR_MODE_3_INDEX(+, ADDR_MODE_3_RM), ADDR_MODE_3_WRITEBACK(ADDR_MODE_3_ADDRESS), BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## I, ADDR_MODE_3_RN, ADDR_MODE_3_WRITEBACK(ADDR_MODE_3_INDEX(-, ADDR_MODE_3_IMMEDIATE)), BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## IU, ADDR_MODE_3_RN, ADDR_MODE_3_WRITEBACK(ADDR_MODE_3_INDEX(+, ADDR_MODE_3_IMMEDIATE)), BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## IP, ADDR_MODE_3_INDEX(-, ADDR_MODE_3_IMMEDIATE), , BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## IPW, ADDR_MODE_3_INDEX(-, ADDR_MODE_3_IMMEDIATE), ADDR_MODE_3_WRITEBACK(ADDR_MODE_3_ADDRESS), BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## IPU, ADDR_MODE_3_INDEX(+, ADDR_MODE_3_IMMEDIATE), , BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## IPUW, ADDR_MODE_3_INDEX(+, ADDR_MODE_3_IMMEDIATE), ADDR_MODE_3_WRITEBACK(ADDR_MODE_3_ADDRESS), BODY) \

#define DEFINE_LOAD_STORE_T_INSTRUCTION_SHIFTER_ARM(NAME, SHIFTER, BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME, SHIFTER, ADDR_MODE_2_WRITEBACK(ADDR_MODE_2_INDEX(-, ADDR_MODE_2_RM)), BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## U, SHIFTER, ADDR_MODE_2_WRITEBACK(ADDR_MODE_2_INDEX(+, ADDR_MODE_2_RM)), BODY) \

#define DEFINE_LOAD_STORE_T_INSTRUCTION_ARM(NAME, BODY) \
	DEFINE_LOAD_STORE_T_INSTRUCTION_SHIFTER_ARM(NAME ## _LSL_, ADDR_MODE_2_LSL, BODY) \
	DEFINE_LOAD_STORE_T_INSTRUCTION_SHIFTER_ARM(NAME ## _LSR_, ADDR_MODE_2_LSR, BODY) \
	DEFINE_LOAD_STORE_T_INSTRUCTION_SHIFTER_ARM(NAME ## _ASR_, ADDR_MODE_2_ASR, BODY) \
	DEFINE_LOAD_STORE_T_INSTRUCTION_SHIFTER_ARM(NAME ## _ROR_, ADDR_MODE_2_ROR, BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## I, ADDR_MODE_2_RN, ADDR_MODE_2_WRITEBACK(ADDR_MODE_2_INDEX(-, ADDR_MODE_2_IMMEDIATE)), BODY) \
	DEFINE_LOAD_STORE_INSTRUCTION_EX_ARM(NAME ## IU, ADDR_MODE_2_RN, ADDR_MODE_2_WRITEBACK(ADDR_MODE_2_INDEX(+, ADDR_MODE_2_IMMEDIATE)), BODY) \

#define ARM_MS_PRE \
	enum PrivilegeMode privilegeMode = cpu->privilegeMode; \
	ARMSetPrivilegeMode(cpu, MODE_SYSTEM);

#define ARM_MS_POST ARMSetPrivilegeMode(cpu, privilegeMode);

#define ADDR_MODE_4_DA uint32_t addr = cpu->gprs[rn]
#define ADDR_MODE_4_IA uint32_t addr = cpu->gprs[rn]
#define ADDR_MODE_4_DB uint32_t addr = cpu->gprs[rn] - 4
#define ADDR_MODE_4_IB uint32_t addr = cpu->gprs[rn] + 4
#define ADDR_MODE_4_DAW cpu->gprs[rn] = addr
#define ADDR_MODE_4_IAW cpu->gprs[rn] = addr
#define ADDR_MODE_4_DBW cpu->gprs[rn] = addr + 4
#define ADDR_MODE_4_IBW cpu->gprs[rn] = addr - 4

#define ARM_M_INCREMENT(BODY) \
	for (m = rs, i = 0; m; m >>= 1, ++i) { \
		if (m & 1) { \
			BODY; \
			addr += 4; \
			total += 1; \
		} \
	}

#define ARM_M_DECREMENT(BODY) \
	for (m = 0x8000, i = 15; m; m >>= 1, --i) { \
		if (rs & m) { \
			BODY; \
			addr -= 4; \
			total += 1; \
		} \
	}

#define DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_EX_ARM(NAME, ADDRESS, WRITEBACK, LOOP, S_PRE, S_POST, BODY, POST_BODY) \
	DEFINE_INSTRUCTION_ARM(NAME, \
		int rn = (opcode >> 16) & 0xF; \
		int rs = opcode & 0x0000FFFF; \
		int m; \
		int i; \
		int total = 0; \
		ADDRESS; \
		S_PRE; \
		LOOP(BODY); \
		S_POST; \
		WRITEBACK; \
		currentCycles += cpu->memory->waitMultiple(cpu->memory, addr, total); \
		POST_BODY;)


#define DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_ARM(NAME, BODY, POST_BODY) \
	DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_EX_ARM(NAME ## DA,   ADDR_MODE_4_DA,                , ARM_M_DECREMENT, , , BODY, POST_BODY) \
	DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_EX_ARM(NAME ## DAW,  ADDR_MODE_4_DA, ADDR_MODE_4_DAW, ARM_M_DECREMENT, , , BODY, POST_BODY) \
	DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_EX_ARM(NAME ## DB,   ADDR_MODE_4_DB,                , ARM_M_DECREMENT, , , BODY, POST_BODY) \
	DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_EX_ARM(NAME ## DBW,  ADDR_MODE_4_DB, ADDR_MODE_4_DBW, ARM_M_DECREMENT, , , BODY, POST_BODY) \
	DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_EX_ARM(NAME ## IA,   ADDR_MODE_4_IA,                , ARM_M_INCREMENT, , , BODY, POST_BODY) \
	DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_EX_ARM(NAME ## IAW,  ADDR_MODE_4_IA, ADDR_MODE_4_IAW, ARM_M_INCREMENT, , , BODY, POST_BODY) \
	DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_EX_ARM(NAME ## IB,   ADDR_MODE_4_IB,                , ARM_M_INCREMENT, , , BODY, POST_BODY) \
	DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_EX_ARM(NAME ## IBW,  ADDR_MODE_4_IB, ADDR_MODE_4_IBW, ARM_M_INCREMENT, , , BODY, POST_BODY) \
	DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_EX_ARM(NAME ## SDA,  ADDR_MODE_4_DA,                , ARM_M_DECREMENT, ARM_MS_PRE, ARM_MS_POST, BODY, POST_BODY) \
	DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_EX_ARM(NAME ## SDAW, ADDR_MODE_4_DA, ADDR_MODE_4_DAW, ARM_M_DECREMENT, ARM_MS_PRE, ARM_MS_POST, BODY, POST_BODY) \
	DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_EX_ARM(NAME ## SDB,  ADDR_MODE_4_DB,                , ARM_M_DECREMENT, ARM_MS_PRE, ARM_MS_POST, BODY, POST_BODY) \
	DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_EX_ARM(NAME ## SDBW, ADDR_MODE_4_DB, ADDR_MODE_4_DBW, ARM_M_DECREMENT, ARM_MS_PRE, ARM_MS_POST, BODY, POST_BODY) \
	DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_EX_ARM(NAME ## SIA,  ADDR_MODE_4_IA,                , ARM_M_INCREMENT, ARM_MS_PRE, ARM_MS_POST, BODY, POST_BODY) \
	DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_EX_ARM(NAME ## SIAW, ADDR_MODE_4_IA, ADDR_MODE_4_IAW, ARM_M_INCREMENT, ARM_MS_PRE, ARM_MS_POST, BODY, POST_BODY) \
	DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_EX_ARM(NAME ## SIB,  ADDR_MODE_4_IB,                , ARM_M_INCREMENT, ARM_MS_PRE, ARM_MS_POST, BODY, POST_BODY) \
	DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_EX_ARM(NAME ## SIBW, ADDR_MODE_4_IB, ADDR_MODE_4_IBW, ARM_M_INCREMENT, ARM_MS_PRE, ARM_MS_POST, BODY, POST_BODY)

// Begin ALU definitions

DEFINE_ALU_INSTRUCTION_ARM(ADD, ARM_ADDITION_S(n, cpu->shifterOperand, cpu->gprs[rd]),
	int32_t n = cpu->gprs[rn];
	cpu->gprs[rd] = n + cpu->shifterOperand;)

DEFINE_ALU_INSTRUCTION_ARM(ADC, ARM_ADDITION_S(cpu->gprs[rn], shifterOperand, cpu->gprs[rd]),
	int32_t n = cpu->gprs[rn];
	int32_t shifterOperand = cpu->shifterOperand + cpu->cpsr.c;
	cpu->gprs[rd] = n + shifterOperand;)

DEFINE_ALU_INSTRUCTION_ARM(AND, ARM_NEUTRAL_S(cpu->gprs[rn], cpu->shifterOperand, cpu->gprs[rd]),
	cpu->gprs[rd] = cpu->gprs[rn] & cpu->shifterOperand;)

DEFINE_ALU_INSTRUCTION_ARM(BIC, ARM_NEUTRAL_S(cpu->gprs[rn], cpu->shifterOperand, cpu->gprs[rd]),
	cpu->gprs[rd] = cpu->gprs[rn] & ~cpu->shifterOperand;)

DEFINE_ALU_INSTRUCTION_S_ONLY_ARM(CMN, ARM_ADDITION_S(cpu->gprs[rn], cpu->shifterOperand, aluOut),
	int32_t aluOut = cpu->gprs[rn] + cpu->shifterOperand;)

DEFINE_ALU_INSTRUCTION_S_ONLY_ARM(CMP, ARM_SUBTRACTION_S(cpu->gprs[rn], cpu->shifterOperand, aluOut),
	int32_t aluOut = cpu->gprs[rn] - cpu->shifterOperand;)

DEFINE_ALU_INSTRUCTION_ARM(EOR, ARM_NEUTRAL_S(cpu->gprs[rn], cpu->shifterOperand, cpu->gprs[rd]),
	cpu->gprs[rd] = cpu->gprs[rn] ^ cpu->shifterOperand;)

DEFINE_ALU_INSTRUCTION_ARM(MOV, ARM_NEUTRAL_S(cpu->gprs[rn], cpu->shifterOperand, cpu->gprs[rd]),
	cpu->gprs[rd] = cpu->shifterOperand;)

DEFINE_ALU_INSTRUCTION_ARM(MVN, ARM_NEUTRAL_S(cpu->gprs[rn], cpu->shifterOperand, cpu->gprs[rd]),
	cpu->gprs[rd] = ~cpu->shifterOperand;)

DEFINE_ALU_INSTRUCTION_ARM(ORR, ARM_NEUTRAL_S(cpu->gprs[rn], cpu->shifterOperand, cpu->gprs[rd]),
	cpu->gprs[rd] = cpu->gprs[rn] | cpu->shifterOperand;)

DEFINE_ALU_INSTRUCTION_ARM(RSB, ARM_SUBTRACTION_S(cpu->shifterOperand, n, cpu->gprs[rd]),
	int32_t n = cpu->gprs[rn];
	cpu->gprs[rd] = cpu->shifterOperand - n;)

DEFINE_ALU_INSTRUCTION_ARM(RSC, ARM_SUBTRACTION_S(cpu->shifterOperand, n, cpu->gprs[rd]),
	int32_t n = cpu->gprs[rn] + !cpu->cpsr.c;
	cpu->gprs[rd] = cpu->shifterOperand - n;)

DEFINE_ALU_INSTRUCTION_ARM(SBC, ARM_SUBTRACTION_S(n, shifterOperand, cpu->gprs[rd]),
	int32_t n = cpu->gprs[rn];
	int32_t shifterOperand = cpu->shifterOperand + !cpu->cpsr.c;
	cpu->gprs[rd] = n - shifterOperand;)

DEFINE_ALU_INSTRUCTION_ARM(SUB, ARM_SUBTRACTION_S(n, cpu->shifterOperand, cpu->gprs[rd]),
	int32_t n = cpu->gprs[rn];
	cpu->gprs[rd] = n - cpu->shifterOperand;)

DEFINE_ALU_INSTRUCTION_S_ONLY_ARM(TEQ, ARM_NEUTRAL_S(cpu->gprs[rn], cpu->shifterOperand, aluOut),
	int32_t aluOut = cpu->gprs[rn] ^ cpu->shifterOperand;)

DEFINE_ALU_INSTRUCTION_S_ONLY_ARM(TST, ARM_NEUTRAL_S(cpu->gprs[rn], cpu->shifterOperand, aluOut),
	int32_t aluOut = cpu->gprs[rn] & cpu->shifterOperand;)

// End ALU definitions

// Begin multiply definitions

DEFINE_MULTIPLY_INSTRUCTION_ARM(MLA, cpu->gprs[rdHi] = cpu->gprs[rm] * cpu->gprs[rs] + cpu->gprs[rd], ARM_NEUTRAL_S(, , cpu->gprs[rdHi]))
DEFINE_MULTIPLY_INSTRUCTION_ARM(MUL, cpu->gprs[rdHi] = cpu->gprs[rm] * cpu->gprs[rs], ARM_NEUTRAL_S(cpu->gprs[rm], cpu->gprs[rs], cpu->gprs[rd]))

DEFINE_MULTIPLY_INSTRUCTION_ARM(SMLAL,
	int64_t d = ((int64_t) cpu->gprs[rm]) * ((int64_t) cpu->gprs[rs]);
	int32_t dm = cpu->gprs[rd];
	int32_t dn = d;
	cpu->gprs[rd] = dm + dn;
	cpu->gprs[rdHi] = cpu->gprs[rdHi] + (d >> 32) + ARM_CARRY_FROM(dm, dn, cpu->gprs[rd]);,
	ARM_NEUTRAL_HI_S(cpu->gprs[rd], cpu->gprs[rdHi]))

DEFINE_MULTIPLY_INSTRUCTION_ARM(SMULL,
	int64_t d = ((int64_t) cpu->gprs[rm]) * ((int64_t) cpu->gprs[rs]);
	cpu->gprs[rd] = d;
	cpu->gprs[rdHi] = d >> 32;,
	ARM_NEUTRAL_HI_S(cpu->gprs[rd], cpu->gprs[rdHi]))

DEFINE_MULTIPLY_INSTRUCTION_ARM(UMLAL,
	uint64_t d = ((uint64_t) cpu->gprs[rm]) * ((uint64_t) cpu->gprs[rs]);
	int32_t dm = cpu->gprs[rd];
	int32_t dn = d;
	cpu->gprs[rd] = dm + dn;
	cpu->gprs[rdHi] = cpu->gprs[rdHi] + (d >> 32) + ARM_CARRY_FROM(dm, dn, cpu->gprs[rd]);,
	ARM_NEUTRAL_HI_S(cpu->gprs[rd], cpu->gprs[rdHi]))

DEFINE_MULTIPLY_INSTRUCTION_ARM(UMULL,
	uint64_t d = ((uint64_t) cpu->gprs[rm]) * ((uint64_t) cpu->gprs[rs]);
	cpu->gprs[rd] = d;
	cpu->gprs[rdHi] = d >> 32;,
	ARM_NEUTRAL_HI_S(cpu->gprs[rd], cpu->gprs[rdHi]))

// End multiply definitions

// Begin load/store definitions

DEFINE_LOAD_STORE_INSTRUCTION_ARM(LDR, cpu->gprs[rd] = cpu->memory->load32(cpu->memory, address, &currentCycles); ARM_LOAD_POST_BODY;)
DEFINE_LOAD_STORE_INSTRUCTION_ARM(LDRB, cpu->gprs[rd] = cpu->memory->loadU8(cpu->memory, address, &currentCycles); ARM_LOAD_POST_BODY;)
DEFINE_LOAD_STORE_MODE_3_INSTRUCTION_ARM(LDRH, cpu->gprs[rd] = cpu->memory->loadU16(cpu->memory, address, &currentCycles); ARM_LOAD_POST_BODY;)
DEFINE_LOAD_STORE_MODE_3_INSTRUCTION_ARM(LDRSB, cpu->gprs[rd] = cpu->memory->load8(cpu->memory, address, &currentCycles); ARM_LOAD_POST_BODY;)
DEFINE_LOAD_STORE_MODE_3_INSTRUCTION_ARM(LDRSH, cpu->gprs[rd] = cpu->memory->load16(cpu->memory, address, &currentCycles); ARM_LOAD_POST_BODY;)
DEFINE_LOAD_STORE_INSTRUCTION_ARM(STR, cpu->memory->store32(cpu->memory, address, cpu->gprs[rd], &currentCycles); ARM_STORE_POST_BODY;)
DEFINE_LOAD_STORE_INSTRUCTION_ARM(STRB, cpu->memory->store8(cpu->memory, address, cpu->gprs[rd], &currentCycles); ARM_STORE_POST_BODY;)
DEFINE_LOAD_STORE_MODE_3_INSTRUCTION_ARM(STRH, cpu->memory->store16(cpu->memory, address, cpu->gprs[rd], &currentCycles); ARM_STORE_POST_BODY;)

DEFINE_LOAD_STORE_T_INSTRUCTION_ARM(LDRBT,
	enum PrivilegeMode priv = cpu->privilegeMode;
	ARMSetPrivilegeMode(cpu, MODE_USER);
	cpu->gprs[rd] = cpu->memory->loadU8(cpu->memory, address, &currentCycles);
	ARMSetPrivilegeMode(cpu, priv);
	ARM_LOAD_POST_BODY;)

DEFINE_LOAD_STORE_T_INSTRUCTION_ARM(LDRT,
	enum PrivilegeMode priv = cpu->privilegeMode;
	ARMSetPrivilegeMode(cpu, MODE_USER);
	cpu->gprs[rd] = cpu->memory->load32(cpu->memory, address, &currentCycles);
	ARMSetPrivilegeMode(cpu, priv);
	ARM_LOAD_POST_BODY;)

DEFINE_LOAD_STORE_T_INSTRUCTION_ARM(STRBT,
	enum PrivilegeMode priv = cpu->privilegeMode;
	ARMSetPrivilegeMode(cpu, MODE_USER);
	cpu->memory->store32(cpu->memory, address, cpu->gprs[rd], &currentCycles);
	ARMSetPrivilegeMode(cpu, priv);
	ARM_STORE_POST_BODY;)

DEFINE_LOAD_STORE_T_INSTRUCTION_ARM(STRT,
	enum PrivilegeMode priv = cpu->privilegeMode;
	ARMSetPrivilegeMode(cpu, MODE_USER);
	cpu->memory->store8(cpu->memory, address, cpu->gprs[rd], &currentCycles);
	ARMSetPrivilegeMode(cpu, priv);
	ARM_STORE_POST_BODY;)

DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_ARM(LDM,
	cpu->gprs[i] = cpu->memory->load32(cpu->memory, addr, 0);,
	++currentCycles;
	if (rs & 0x8000) {
		ARM_WRITE_PC;
	})

DEFINE_LOAD_STORE_MULTIPLE_INSTRUCTION_ARM(STM,
	cpu->memory->store32(cpu->memory, addr, cpu->gprs[i], 0);,
	currentCycles -= ARM_PREFETCH_CYCLES)

DEFINE_INSTRUCTION_ARM(SWP, ARM_STUB)
DEFINE_INSTRUCTION_ARM(SWPB, ARM_STUB)

// End load/store definitions

// Begin branch definitions

DEFINE_INSTRUCTION_ARM(B,
	int32_t offset = opcode << 8;
	offset >>= 6;
	cpu->gprs[ARM_PC] += offset;
	ARM_WRITE_PC;)

DEFINE_INSTRUCTION_ARM(BL,
	int32_t immediate = (opcode & 0x00FFFFFF) << 8;
	cpu->gprs[ARM_LR] = cpu->gprs[ARM_PC] - WORD_SIZE_ARM;
	cpu->gprs[ARM_PC] += immediate >> 6;
	ARM_WRITE_PC;)

DEFINE_INSTRUCTION_ARM(BX,
	int rm = opcode & 0x0000000F;
	_ARMSetMode(cpu, cpu->gprs[rm] & 0x00000001);
	cpu->gprs[ARM_PC] = cpu->gprs[rm] & 0xFFFFFFFE;
	if (cpu->executionMode == MODE_THUMB) {
		THUMB_WRITE_PC;
	} else {
		ARM_WRITE_PC;
	})

// End branch definitions

// Begin miscellaneous definitions

DEFINE_INSTRUCTION_ARM(BKPT, ARM_STUB) // Not strictly in ARMv4T, but here for convenience
DEFINE_INSTRUCTION_ARM(ILL, ARM_STUB) // Illegal opcode

DEFINE_INSTRUCTION_ARM(MSR,
	int c = opcode & 0x00010000;
	int f = opcode & 0x00080000;
	int32_t operand = cpu->gprs[opcode & 0x0000000F];
	int32_t mask = (c ? 0x000000FF : 0) | (f ? 0xFF000000 : 0);
	if (mask & PSR_USER_MASK) {
		cpu->cpsr.packed = (cpu->cpsr.packed & ~PSR_USER_MASK) | (operand & PSR_USER_MASK);
	}
	if (cpu->privilegeMode != MODE_USER && (mask & PSR_PRIV_MASK)) {
		ARMSetPrivilegeMode(cpu, (enum PrivilegeMode) ((operand & 0x0000000F) | 0x00000010));
		cpu->cpsr.packed = (cpu->cpsr.packed & ~PSR_PRIV_MASK) | (operand & PSR_PRIV_MASK);
	})

DEFINE_INSTRUCTION_ARM(MSRR,
	int c = opcode & 0x00010000;
	int f = opcode & 0x00080000;
	int32_t operand = cpu->gprs[opcode & 0x0000000F];
	int32_t mask = (c ? 0x000000FF : 0) | (f ? 0xFF000000 : 0);
	mask &= PSR_USER_MASK | PSR_PRIV_MASK | PSR_STATE_MASK;
	cpu->spsr.packed = (cpu->spsr.packed & ~mask) | (operand & mask);)

DEFINE_INSTRUCTION_ARM(MRS, \
	int rd = (opcode >> 12) & 0xF; \
	cpu->gprs[rd] = cpu->cpsr.packed;)

DEFINE_INSTRUCTION_ARM(MRSR, \
	int rd = (opcode >> 12) & 0xF; \
	cpu->gprs[rd] = cpu->spsr.packed;)

DEFINE_INSTRUCTION_ARM(MSRI,
	int c = opcode & 0x00010000;
	int f = opcode & 0x00080000;
	int rotate = (opcode & 0x00000F00) >> 8;
	int32_t operand = ARM_ROR(opcode & 0x000000FF, rotate);
	int32_t mask = (c ? 0x000000FF : 0) | (f ? 0xFF000000 : 0);
	if (mask & PSR_USER_MASK) {
		cpu->cpsr.packed = (cpu->cpsr.packed & ~PSR_USER_MASK) | (operand & PSR_USER_MASK);
	}
	if (cpu->privilegeMode != MODE_USER && (mask & PSR_PRIV_MASK)) {
		ARMSetPrivilegeMode(cpu, (enum PrivilegeMode) ((operand & 0x0000000F) | 0x00000010));
		cpu->cpsr.packed = (cpu->cpsr.packed & ~PSR_PRIV_MASK) | (operand & PSR_PRIV_MASK);
	})

DEFINE_INSTRUCTION_ARM(MSRRI,
	int c = opcode & 0x00010000;
	int f = opcode & 0x00080000;
	int rotate = (opcode & 0x00000F00) >> 8;
	int32_t operand = ARM_ROR(opcode & 0x000000FF, rotate);
	int32_t mask = (c ? 0x000000FF : 0) | (f ? 0xFF000000 : 0);
	mask &= PSR_USER_MASK | PSR_PRIV_MASK | PSR_STATE_MASK;
	cpu->spsr.packed = (cpu->spsr.packed & ~mask) | (operand & mask);)

DEFINE_INSTRUCTION_ARM(SWI, cpu->board->swi32(cpu->board, opcode & 0xFFFFFF))

#define DECLARE_INSTRUCTION_ARM(EMITTER, NAME) \
	EMITTER ## NAME

#define DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, ALU) \
	DO_8(DECLARE_INSTRUCTION_ARM(EMITTER, ALU ## I)), \
	DO_8(DECLARE_INSTRUCTION_ARM(EMITTER, ALU ## I))

#define DECLARE_ARM_ALU_BLOCK(EMITTER, ALU, EX1, EX2, EX3, EX4) \
	DECLARE_INSTRUCTION_ARM(EMITTER, ALU ## _LSL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ALU ## _LSLR), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ALU ## _LSR), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ALU ## _LSRR), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ALU ## _ASR), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ALU ## _ASRR), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ALU ## _ROR), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ALU ## _RORR), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ALU ## _LSL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, EX1), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ALU ## _LSR), \
	DECLARE_INSTRUCTION_ARM(EMITTER, EX2), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ALU ## _ASR), \
	DECLARE_INSTRUCTION_ARM(EMITTER, EX3), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ALU ## _ROR), \
	DECLARE_INSTRUCTION_ARM(EMITTER, EX4)

#define DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, NAME, P, U, W) \
	DO_8(DECLARE_INSTRUCTION_ARM(EMITTER, NAME ## I ## P ## U ## W)), \
	DO_8(DECLARE_INSTRUCTION_ARM(EMITTER, NAME ## I ## P ## U ## W))

#define DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, NAME, P, U, W) \
	DECLARE_INSTRUCTION_ARM(EMITTER, NAME ## _LSL_ ## P ## U ## W), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, NAME ## _LSR_ ## P ## U ## W), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, NAME ## _ASR_ ## P ## U ## W), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, NAME ## _ROR_ ## P ## U ## W), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, NAME ## _LSL_ ## P ## U ## W), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, NAME ## _LSR_ ## P ## U ## W), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, NAME ## _ASR_ ## P ## U ## W), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, NAME ## _ROR_ ## P ## U ## W), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL)

#define DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, NAME, MODE, W) \
	DO_8(DECLARE_INSTRUCTION_ARM(EMITTER, NAME ## MODE ## W)), \
	DO_8(DECLARE_INSTRUCTION_ARM(EMITTER, NAME ## MODE ## W))

#define DECLARE_ARM_BRANCH_BLOCK(EMITTER, NAME) \
	DO_256(DECLARE_INSTRUCTION_ARM(EMITTER, NAME))

// TODO: Support coprocessors
#define DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, NAME, P, U, W, N) \
	DO_8(0), \
	DO_8(0)

#define DECLARE_ARM_COPROCESSOR_BLOCK(EMITTER, NAME1, NAME2) \
	DO_8(DO_8(DO_INTERLACE(0, 0))), \
	DO_8(DO_8(DO_INTERLACE(0, 0)))

#define DECLARE_ARM_SWI_BLOCK(EMITTER) \
	DO_256(DECLARE_INSTRUCTION_ARM(EMITTER, SWI))

#define DECLARE_ARM_EMITTER_BLOCK(EMITTER) \
	DECLARE_ARM_ALU_BLOCK(EMITTER, AND, MUL, STRH, ILL, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, ANDS, MULS, LDRH, LDRSB, LDRSH), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, EOR, MLA, ILL, ILL, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, EORS, MLAS, ILL, ILL, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, SUB, ILL, STRHI, ILL, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, SUBS, ILL, LDRHI, LDRSBI, LDRSHI), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, RSB, ILL, ILL, ILL, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, RSBS, ILL, ILL, ILL, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, ADD, UMULL, STRHU, ILL, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, ADDS, UMULLS, LDRHU, LDRSBU, LDRSHU), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, ADC, UMLAL, ILL, ILL, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, ADCS, UMLALS, ILL, ILL, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, SBC, SMULL, STRHIU, ILL, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, SBCS, SMULLS, LDRHIU, LDRSBIU, LDRSHIU), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, RSC, SMLAL, ILL, ILL, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, RSCS, SMLALS, ILL, ILL, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, MRS), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, SWP), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, STRHP), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, TST, ILL, LDRHP, LDRSBP, LDRSHP), \
	DECLARE_INSTRUCTION_ARM(EMITTER, MSR), \
	DECLARE_INSTRUCTION_ARM(EMITTER, BX), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, BKPT), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, STRHPW), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, TEQ, ILL, LDRHPW, LDRSBPW, LDRSHPW), \
	DECLARE_INSTRUCTION_ARM(EMITTER, MRSR), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, SWPB), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, STRHIP), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, CMP, ILL, LDRHIP, LDRSBIP, LDRSHIP), \
	DECLARE_INSTRUCTION_ARM(EMITTER, MSRR), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, STRHIPW), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_INSTRUCTION_ARM(EMITTER, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, CMN, ILL, LDRHIPW, LDRSBIPW, LDRSHIPW), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, ORR, SMLAL, STRHPU, ILL, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, ORRS, SMLALS, LDRHPU, LDRSBPU, LDRSHPU), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, MOV, SMLAL, STRHPUW, ILL, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, MOVS, SMLALS, LDRHPUW, LDRSBPUW, LDRSHPUW), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, BIC, SMLAL, STRHIPU, ILL, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, BICS, SMLALS, LDRHIPU, LDRSBIPU, LDRSHIPU), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, MVN, SMLAL, STRHIPUW, ILL, ILL), \
	DECLARE_ARM_ALU_BLOCK(EMITTER, MVNS, SMLALS, LDRHIPUW, LDRSBIPUW, LDRSHIPUW), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, AND), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, ANDS), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, EOR), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, EORS), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, SUB), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, SUBS), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, RSB), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, RSBS), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, ADD), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, ADDS), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, ADC), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, ADCS), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, SBC), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, SBCS), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, RSC), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, RSCS), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, TST), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, TST), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, MSR), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, TEQ), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, CMP), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, CMP), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, MSRR), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, CMN), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, ORR), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, ORRS), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, MOV), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, MOVS), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, BIC), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, BICS), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, MVN), \
	DECLARE_ARM_ALU_IMMEDIATE_BLOCK(EMITTER, MVNS), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, STR, , , ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, LDR, , , ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, STRT, , , ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, LDRT, , , ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, STRB, , , ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, LDRB, , , ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, STRBT, , , ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, LDRBT, , , ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, STR, , U, ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, LDR, , U, ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, STRT, , U, ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, LDRT, , U, ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, STRB, , U, ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, LDRB, , U, ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, STRBT, , U, ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, LDRBT, , U, ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, STR, P, , ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, LDR, P, , ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, STR, P, , W), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, LDR, P, , W), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, STRB, P, , ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, LDRB, P, , ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, STRB, P, , W), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, LDRB, P, , W), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, STR, P, U, ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, LDR, P, U, ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, STR, P, U, W), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, LDR, P, U, W), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, STRB, P, U, ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, LDRB, P, U, ), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, STRB, P, U, W), \
	DECLARE_ARM_LOAD_STORE_IMMEDIATE_BLOCK(EMITTER, LDRB, P, U, W), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, STR, , , ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, LDR, , , ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, STRT, , , ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, LDRT, , , ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, STRB, , , ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, LDRB, , , ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, STRBT, , , ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, LDRBT, , , ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, STR, , U, ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, LDR, , U, ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, STRT, , U, ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, LDRT, , U, ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, STRB, , U, ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, LDRB, , U, ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, STRBT, , U, ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, LDRBT, , U, ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, STR, P, , ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, LDR, P, , ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, STR, P, , W), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, LDR, P, , W), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, STRB, P, , ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, LDRB, P, , ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, STRB, P, , W), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, LDRB, P, , W), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, STR, P, U, ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, LDR, P, U, ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, STR, P, U, W), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, LDR, P, U, W), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, STRB, P, U, ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, LDRB, P, U, ), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, STRB, P, U, W), \
	DECLARE_ARM_LOAD_STORE_BLOCK(EMITTER, LDRB, P, U, W), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, STM, DA, ), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, LDM, DA, ), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, STM, DA, W), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, LDM, DA, W), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, STMS, DA, ), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, LDMS, DA, ), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, STMS, DA, W), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, LDMS, DA, W), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, STM, IA, ), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, LDM, IA, ), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, STM, IA, W), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, LDM, IA, W), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, STMS, IA, ), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, LDMS, IA, ), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, STMS, IA, W), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, LDMS, IA, W), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, STM, DB, ), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, LDM, DB, ), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, STM, DB, W), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, LDM, DB, W), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, STMS, DB, ), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, LDMS, DB, ), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, STMS, DB, W), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, LDMS, DB, W), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, STM, IB, ), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, LDM, IB, ), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, STM, IB, W), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, LDM, IB, W), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, STMS, IB, ), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, LDMS, IB, ), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, STMS, IB, W), \
	DECLARE_ARM_LOAD_STORE_MULTIPLE_BLOCK(EMITTER, LDMS, IB, W), \
	DECLARE_ARM_BRANCH_BLOCK(EMITTER, B), \
	DECLARE_ARM_BRANCH_BLOCK(EMITTER, BL), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, STC, , , , ), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, LDC, , , , ), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, STC, , , , W), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, LDC, , , , W), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, STC, , , N, ), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, LDC, , , N, ), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, STC, , , N, W), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, LDC, , , N, W), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, STC, , U, , ), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, LDC, , U, , ), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, STC, , U, , W), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, LDC, , U, , W), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, STC, , U, N, ), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, LDC, , U, N, ), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, STC, , U, N, W), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, LDC, , U, N, W), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, STC, P, , , ), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, LDC, P, , , ), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, STC, P, , , W), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, LDC, P, , , W), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, STC, P, U, N, ), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, LDC, P, U, N, ), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, STC, P, U, N, W), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, LDC, P, U, N, W), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, STC, P, , N, ), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, LDC, P, , N, ), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, STC, P, , N, W), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, LDC, P, , N, W), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, STC, P, U, N, ), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, LDC, P, U, N, ), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, STC, P, U, N, W), \
	DECLARE_ARM_LOAD_STORE_COPROCESSOR_BLOCK(EMITTER, LDC, P, U, N, W), \
	DECLARE_ARM_COPROCESSOR_BLOCK(EMITTER, CDP, MCR), \
	DECLARE_ARM_SWI_BLOCK(EMITTER)

static const ARMInstruction _armTable[0x1000] = {
	DECLARE_ARM_EMITTER_BLOCK(_ARMInstruction)
};
