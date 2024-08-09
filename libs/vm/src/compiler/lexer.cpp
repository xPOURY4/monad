#include "compiler/lexer.h"

static inline block_id curr_block_id(std::vector<Block> blocks) {
  return blocks.size() - 1;
}

static inline void add_jump_dest(LexerResult *result, byte_offset curr_offset) {
  result->jumpdests.emplace(curr_offset, curr_block_id(result->blocks));
}

static inline void add_block(LexerResult *result, byte_offset instr_begin) {
  Block block = {instr_begin, 0, Terminator::Stop, std::nullopt};
  result->blocks.emplace_back(block);
}

static inline void add_terminator(LexerResult *result, Terminator t) {
  result->blocks.back().terminator = t;
}

static inline void add_fallthrough_terminator(LexerResult *result,
                                              Terminator t) {
  result->blocks.back().terminator = t;
  result->blocks.back().fallthrough_dest = curr_block_id(result->blocks) + 1;
}

void lex(std::vector<uint8_t> *byte_code, LexerResult *result) {

  enum class St { INSIDE_BLOCK, OUTSIDE_BLOCK };

  byte_offset curr_offset = 0;
  St st = St::INSIDE_BLOCK;

  add_block(result, 0);

  while (curr_offset < byte_code->size()) {
    uint8_t opcode = (*byte_code)[curr_offset];
    if (st == St::OUTSIDE_BLOCK) {
      if (opcode == JUMPDEST) {
        add_block(result, curr_offset + 1);
        st = St::INSIDE_BLOCK;
        add_jump_dest(result, curr_offset);
      }
    } else {
      assert(st == St::INSIDE_BLOCK);
      switch (opcode) {
      case JUMPDEST:
        if (result->blocks.back().num_instrs > 0) { // jumpdest terminator
          add_fallthrough_terminator(result, Terminator::JumpDest);
          add_block(result, curr_offset + 1);
        } else { // otherwise continue with this block
          result->blocks.back().instr_begin++;
        }
        add_jump_dest(result, curr_offset);
        break;

      case JUMPI:
        add_fallthrough_terminator(result, Terminator::JumpI);
        add_block(result, curr_offset + 1);
        break;

      case JUMP:
        add_terminator(result, Terminator::Jump);
        st = St::OUTSIDE_BLOCK;
        break;

      case RETURN:
        add_terminator(result, Terminator::Return);
        st = St::OUTSIDE_BLOCK;
        break;

      case STOP:
        add_terminator(result, Terminator::Stop);
        st = St::OUTSIDE_BLOCK;
        break;

      case REVERT:
        add_terminator(result, Terminator::Revert);
        st = St::OUTSIDE_BLOCK;
        break;

      case SELFDESTRUCT:
        add_terminator(result, Terminator::SelfDestruct);
        st = St::OUTSIDE_BLOCK;
        break;

      default: // instruction opcode
        result->blocks.back().num_instrs++;
        break;
      }
    }
    curr_offset += 1 + opCodeInfo[opcode].num_args;
  }

  // if ends with an illegal push, pad with zeros
  if (curr_offset > byte_code->size()) {
    byte_code->resize(curr_offset, 0);
  }

  return;
}

OpCodeInfo opCodeInfo[] = {
    {"STOP", 0},       // 0x00
    {"ADD", 0},        // 0x01
    {"MUL", 0},        // 0x02
    {"SUB", 0},        // 0x03
    {"DIV", 0},        // 0x04,
    {"SDIV", 0},       // 0x05,
    {"MOD", 0},        // 0x06,
    {"SMOD", 0},       // 0x07,
    {"ADDMOD", 0},     // 0x08,
    {"MULMOD", 0},     // 0x09,
    {"EXP", 0},        // 0x0A,
    {"SIGNEXTEND", 0}, // 0x0B,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,

    {"LT", 0},     // 0x10,
    {"GT", 0},     // 0x11,
    {"SLT", 0},    // 0x12,
    {"SGT", 0},    // 0x13,
    {"EQ", 0},     // 0x14,
    {"ISZERO", 0}, // 0x15,
    {"AND", 0},    // 0x16,
    {"OR", 0},     // 0x17,
    {"XOR", 0},    // 0x18,
    {"NOT", 0},    // 0x19,
    {"BYTE", 0},   // 0x1A,
    {"SHL", 0},    // 0x1B,
    {"SHR", 0},    // 0x1C,
    {"SAR", 0},    // 0x1D,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,

    {"SHA3", 0}, // 0x20,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,

    {"ADDRESS", 0},        // 0x30,
    {"BALANCE", 0},        // 0x31,
    {"ORIGIN", 0},         // 0x32,
    {"CALLER", 0},         // 0x33,
    {"CALLVALUE", 0},      // 0x34,
    {"CALLDATALOAD", 0},   // 0x35,
    {"CALLDATASIZE", 0},   // 0x36,
    {"CALLDATACOPY", 0},   // 0x37,
    {"CODESIZE", 0},       // 0x38,
    {"CODECOPY", 0},       // 0x39,
    {"GASPRICE", 0},       // 0x3A,
    {"EXTCODESIZE", 0},    // 0x3B,
    {"EXTCODECOPY", 0},    // 0x3C,
    {"RETURNDATASIZE", 0}, // 0x3D,
    {"RETURNDATACOPY", 0}, // 0x3E,
    {"EXTCODEHASH", 0},    // 0x3F,

    {"BLOCKHASH", 0},   // 0x40,
    {"COINBASE", 0},    // 0x41,
    {"TIMESTAMP", 0},   // 0x42,
    {"NUMBER", 0},      // 0x43,
    {"DIFFICULTY", 0},  // 0x44,
    {"GASLIMIT", 0},    // 0x45,
    {"CHAINID", 0},     // 0x46,
    {"SELFBALANCE", 0}, // 0x47,
    {"BASEFEE", 0},     // 0x48,
    {"BLOBHASH", 0},    // 0x49,
    {"BLOBBASEFEE", 0}, // 0x4A,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,

    {"POP", 0},      // 0x50,
    {"MLOAD", 0},    // 0x51,
    {"MSTORE", 0},   // 0x52,
    {"MSTORE8", 0},  // 0x53,
    {"SLOAD", 0},    // 0x54,
    {"SSTORE", 0},   // 0x55,
    {"JUMP", 0},     // 0x56,
    {"JUMPI", 0},    // 0x57,
    {"PC", 0},       // 0x58,
    {"MSIZE", 0},    // 0x59,
    {"GAS", 0},      // 0x5A,
    {"JUMPDEST", 0}, // 0x5B,
    {"TLOAD", 0},    // 0x5C,
    {"TSTORE", 0},   // 0x5D,
    {"MCOPY", 0},    // 0x5E,
    {"PUSH0", 0},    // 0x5F,

    {"PUSH1", 1},   // 0x60,
    {"PUSH2", 2},   // 0x61,
    {"PUSH3", 3},   // 0x62,
    {"PUSH4", 4},   // 0x63,
    {"PUSH5", 5},   // 0x64,
    {"PUSH6", 6},   // 0x65,
    {"PUSH7", 7},   // 0x66,
    {"PUSH8", 8},   // 0x67,
    {"PUSH9", 9},   // 0x68,
    {"PUSH10", 10}, // 0x69,
    {"PUSH11", 11}, // 0x6A,
    {"PUSH12", 12}, // 0x6B,
    {"PUSH13", 13}, // 0x6C,
    {"PUSH14", 14}, // 0x6D,
    {"PUSH15", 15}, // 0x6E,
    {"PUSH16", 16}, // 0x6F,

    {"PUSH17", 17}, // 0x70,
    {"PUSH18", 18}, // 0x71,
    {"PUSH19", 19}, // 0x72,
    {"PUSH20", 20}, // 0x73,
    {"PUSH21", 21}, // 0x74,
    {"PUSH22", 22}, // 0x75,
    {"PUSH23", 23}, // 0x76,
    {"PUSH24", 24}, // 0x77,
    {"PUSH25", 25}, // 0x78,
    {"PUSH26", 26}, // 0x79,
    {"PUSH27", 27}, // 0x7A,
    {"PUSH28", 28}, // 0x7B,
    {"PUSH29", 29}, // 0x7C,
    {"PUSH30", 30}, // 0x7D,
    {"PUSH31", 31}, // 0x7E,
    {"PUSH32", 32}, // 0x7F,

    {"DUP1", 0},  // 0x80,
    {"DUP2", 0},  // 0x81,
    {"DUP3", 0},  // 0x82,
    {"DUP4", 0},  // 0x83,
    {"DUP5", 0},  // 0x84,
    {"DUP6", 0},  // 0x85,
    {"DUP7", 0},  // 0x86,
    {"DUP8", 0},  // 0x87,
    {"DUP9", 0},  // 0x88,
    {"DUP10", 0}, // 0x89,
    {"DUP11", 0}, // 0x8A,
    {"DUP12", 0}, // 0x8B,
    {"DUP13", 0}, // 0x8C,
    {"DUP14", 0}, // 0x8D,
    {"DUP15", 0}, // 0x8E,
    {"DUP16", 0}, // 0x8F,

    {"SWAP1", 0},  // 0x90,
    {"SWAP2", 0},  // 0x91,
    {"SWAP3", 0},  // 0x92,
    {"SWAP4", 0},  // 0x93,
    {"SWAP5", 0},  // 0x94,
    {"SWAP6", 0},  // 0x95,
    {"SWAP7", 0},  // 0x96,
    {"SWAP8", 0},  // 0x97,
    {"SWAP9", 0},  // 0x98,
    {"SWAP10", 0}, // 0x99,
    {"SWAP11", 0}, // 0x9A,
    {"SWAP12", 0}, // 0x9B,
    {"SWAP13", 0}, // 0x9C,
    {"SWAP14", 0}, // 0x9D,
    {"SWAP15", 0}, // 0x9E,
    {"SWAP16", 0}, // 0x9F,

    {"LOG0", 0}, // 0xA0,
    {"LOG1", 0}, // 0xA1,
    {"LOG2", 0}, // 0xA2,
    {"LOG3", 0}, // 0xA3,
    {"LOG4", 0}, // 0xA4,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,

    UNKNOWN_OPCODE_INFO, // 0xB0
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,

    UNKNOWN_OPCODE_INFO, // 0xC0
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,

    UNKNOWN_OPCODE_INFO, // 0xD0
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,

    UNKNOWN_OPCODE_INFO, // 0xE0
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,

    {"CREATE", 0},       // 0xF0,
    {"CALL", 0},         // 0xF1,
    {"CALLCODE", 0},     // 0xF2,
    {"RETURN", 0},       // 0xF3,
    {"DELEGATECALL", 0}, // 0xF4,
    {"CREATE2", 0},      // 0xF5,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    {"STATICCALL", 0}, // 0xFA,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    {"REVERT", 0}, // 0xFD,
    UNKNOWN_OPCODE_INFO,
    {"SELFDESTRUCT", 0} // 0xFF,
};
