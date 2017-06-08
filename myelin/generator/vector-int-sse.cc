#include "myelin/generator/expression.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Generate vector int expression using SSE and XMM registers.
class VectorIntSSEGenerator : public ExpressionGenerator {
 public:
  VectorIntSSEGenerator() {
    model_.mov_reg_reg = true;
    model_.mov_reg_imm = true;
    model_.mov_reg_mem = true;
    model_.mov_mem_reg = true;
    model_.op_reg_reg = true;
    model_.op_reg_mem = true;
    model_.func_reg_reg = true;
    model_.func_reg_mem = true;
  }

  string Name() override { return "VectorIntSSE"; }

  int VectorSize() override { return XMMRegSize; }

  void Reserve() override {
    // Reserve XMM registers for temps.
    index_->ReserveXMMRegisters(instructions_.NumRegs());

    // Reserve auxiliary registers.
    int num_rr_aux = 0;
    int num_mm_aux = 0;
    if (instructions_.Has(Expression::MUL)) {
      if (type_ == DT_INT8) {
        num_mm_aux = std::max(num_mm_aux, 2);
      }
      if (type_ == DT_INT64) {
        num_rr_aux = std::max(num_rr_aux, 2);
        num_mm_aux = std::max(num_mm_aux, 1);
      }
    }
    if (instructions_.Has(Expression::MIN) ||
        instructions_.Has(Expression::MAX) ||
        instructions_.Has(Expression::RELU)) {
      if (type_ == DT_INT64) {
        num_rr_aux = std::max(num_rr_aux, 2);
        num_mm_aux = std::max(num_mm_aux, 1);
      }
    }
    index_->ReserveAuxRegisters(num_rr_aux);
    index_->ReserveAuxXMMRegisters(num_mm_aux);
  }

  void Generate(Expression::Op *instr, MacroAssembler *masm) override {
    switch (instr->type) {
      case Expression::MOV:
        GenerateXMMVectorIntMove(instr, masm);
        break;
      case Expression::ADD:
        GenerateXMMIntOp(instr,
            &Assembler::paddb, &Assembler::paddb,
            &Assembler::paddw, &Assembler::paddw,
            &Assembler::paddd, &Assembler::paddd,
            &Assembler::paddq, &Assembler::paddq,
            masm);
        break;
      case Expression::SUB:
        GenerateXMMIntOp(instr,
            &Assembler::psubb, &Assembler::psubb,
            &Assembler::psubw, &Assembler::psubw,
            &Assembler::psubd, &Assembler::psubd,
            &Assembler::psubq, &Assembler::psubq,  // dummy
            masm);
        break;
      case Expression::MUL:
        switch (type_) {
          case DT_INT8:
            // Multiply even and odd bytes and merge results.
            // See https://stackoverflow.com/a/29155682 for the details.
            // First load operands.
            CHECK(instr->dst != -1);
            __ movdqa(xmmaux(0), xmm(instr->dst));
            if (instr->src != -1) {
              __ movdqa(xmmaux(1), xmm(instr->src));
            } else {
              __ movdqa(xmmaux(1), addr(instr->args[1]));
            }

            // Multiply even bytes.
            __ pmullw(xmm(instr->dst), xmmaux(1));

            // Multiply odd bytes.
            __ psraw(xmmaux(0), 8);
            __ psraw(xmmaux(1), 8);
            __ pmullw(xmmaux(0), xmmaux(1));
            __ psllw(xmmaux(0), 8);

            // Combine even and odd results.
            __ pcmpeqw(xmmaux(1), xmmaux(1));
            __ psrlw(xmmaux(1), 8);  // constant 8 times 0x00FF
            __ pand(xmm(instr->dst), xmmaux(1));
            __ por(xmm(instr->dst), xmmaux(0));
            break;
          case DT_INT16:
          case DT_INT32:
            GenerateXMMIntOp(instr,
                &Assembler::pmullw, &Assembler::pmullw,  // dummy
                &Assembler::pmullw, &Assembler::pmullw,
                &Assembler::pmulld, &Assembler::pmulld,  // only sse 4.1
                &Assembler::pmulld, &Assembler::pmulld,  // dummy
                masm);
            break;
          case DT_INT64: {
            // Multiply each XMM element using x86 multiply.
            CHECK(instr->dst != -1);
            XMMRegister src;
            if (instr->src != -1) {
              src = xmm(instr->src);
            } else {
              src = xmmaux(0);
              __ movdqa(src, addr(instr->args[1]));
            }
            for (int n = 0; n < 2; ++n) {
              __ pextrq(aux(0), xmm(instr->dst), n);
              __ pextrq(aux(1), src, n);
              __ imulq(aux(0), aux(1));
              __ pinsrq(xmm(instr->dst), aux(0), n);
            }
            break;
          }
          default:
            UNSUPPORTED;
        }
        break;
      case Expression::DIV:
        UNSUPPORTED;
        break;
      case Expression::MIN:
        if (type_ == DT_INT64) {
          CHECK(instr->dst != -1);
          XMMRegister src;
          if (instr->src != -1) {
            src = xmm(instr->src);
          } else {
            src = xmmaux(0);
            __ movdqa(src, addr(instr->args[1]));
          }
          for (int n = 0; n < 2; ++n) {
            __ pextrq(aux(0), xmm(instr->dst), n);
            __ pextrq(aux(1), src, n);
            __ cmpq(aux(0), aux(1));
            __ cmovq(greater, aux(0), aux(1));
            __ pinsrq(xmm(instr->dst), aux(0), n);
          }
        } else {
          GenerateXMMIntOp(instr,
              &Assembler::pminsb, &Assembler::pminsb,
              &Assembler::pminsw, &Assembler::pminsw,
              &Assembler::pminsd, &Assembler::pminsd,
              &Assembler::pminsd, &Assembler::pminsd,
              masm);
        }
        break;
      case Expression::MAX:
        if (type_ == DT_INT64) {
          CHECK(instr->dst != -1);
          XMMRegister src;
          if (instr->src != -1) {
            src = xmm(instr->src);
          } else {
            src = xmmaux(0);
            __ movdqa(src, addr(instr->args[1]));
          }
          for (int n = 0; n < 2; ++n) {
            __ pextrq(aux(0), xmm(instr->dst), n);
            __ pextrq(aux(1), src, n);
            __ cmpq(aux(0), aux(1));
            __ cmovq(less, aux(0), aux(1));
            __ pinsrq(xmm(instr->dst), aux(0), n);
          }
        } else {
          GenerateXMMIntOp(instr,
              &Assembler::pmaxsb, &Assembler::pmaxsb,
              &Assembler::pmaxsw, &Assembler::pmaxsw,
              &Assembler::pmaxsd, &Assembler::pmaxsd,
              &Assembler::pmaxsd, &Assembler::pmaxsd,  // dummy
              masm);
        }
        break;
      case Expression::RELU:
        if (type_ == DT_INT64) {
          CHECK(instr->dst != -1);
          XMMRegister src;
          if (instr->src != -1) {
            src = xmm(instr->src);
          } else {
            src = xmmaux(0);
            __ movdqa(src, addr(instr->args[0]));
          }
          Register zero = aux(1);
          __ xorq(zero, zero);
          for (int n = 0; n < 2; ++n) {
            __ pextrq(aux(0), src, n);
            __ testq(aux(0), aux(0));
            __ cmovq(positive, aux(0), zero);
            __ pinsrq(xmm(instr->dst), aux(0), n);
          }
        } else {
          __ pxor(xmm(instr->dst), xmm(instr->dst));
          GenerateXMMIntOp(instr,
              &Assembler::pmaxsb, &Assembler::pmaxsb,
              &Assembler::pmaxsw, &Assembler::pmaxsw,
              &Assembler::pmaxsd, &Assembler::pmaxsd,
              &Assembler::pmaxsd, &Assembler::pmaxsd,  // dummy
              masm, 0);
        }
        break;
      default: UNSUPPORTED;
    }
  }
};

ExpressionGenerator *CreateVectorIntSSEGenerator() {
  return new VectorIntSSEGenerator();
}

}  // namespace myelin
}  // namespace sling
