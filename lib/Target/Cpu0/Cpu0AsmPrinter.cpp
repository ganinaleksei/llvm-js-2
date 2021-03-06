//===-- Cpu0AsmPrinter.cpp - Cpu0 LLVM Assembly Printer -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to GAS-format CPU0 assembly language.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "cpu0-asm-printer"
#include "Cpu0AsmPrinter.h"
#include "Cpu0.h"
#include "Cpu0InstrInfo.h"
#include "InstPrinter/Cpu0InstPrinter.h"
#include "MCTargetDesc/Cpu0BaseInfo.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/SparseSet.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/Mangler.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

void Cpu0AsmPrinter::EmitInstrWithMacroNoAT(const MachineInstr *MI) {
  MCInst TmpInst;

  MCInstLowering.Lower(MI, TmpInst);
  OutStreamer.EmitRawText(StringRef("\t.set\tmacro"));
  if (Cpu0FI->getEmitNOAT())
    OutStreamer.EmitRawText(StringRef("\t.set\tat"));
  OutStreamer.EmitInstruction(TmpInst);
  if (Cpu0FI->getEmitNOAT())
    OutStreamer.EmitRawText(StringRef("\t.set\tnoat"));
  OutStreamer.EmitRawText(StringRef("\t.set\tnomacro"));
}

bool Cpu0AsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  Cpu0FI = MF.getInfo<Cpu0FunctionInfo>();
  AsmPrinter::runOnMachineFunction(MF);
  return true;
}

//- EmitInstruction() must exists or will have run time error.
void Cpu0AsmPrinter::EmitInstruction(const MachineInstr *MI) {
  if (MI->isDebugValue()) {
    SmallString<128> Str;
    raw_svector_ostream OS(Str);

    PrintDebugValueComment(MI, OS);
    return;
  }

  unsigned Opc = MI->getOpcode();
  MCInst TmpInst0;
  SmallVector<MCInst, 4> MCInsts;

  switch (Opc) {
  case Cpu0::CPRESTORE: {
    const MachineOperand &MO = MI->getOperand(0);
    assert(MO.isImm() && "CPRESTORE's operand must be an immediate.");
    int64_t Offset = MO.getImm();

    if (OutStreamer.hasRawTextSupport()) {
      if (!isInt<16>(Offset)) {
        EmitInstrWithMacroNoAT(MI);
        return;
      }
    } else {
      MCInstLowering.LowerCPRESTORE(Offset, MCInsts);

      for (SmallVector<MCInst, 4>::iterator I = MCInsts.begin();
           I != MCInsts.end(); ++I)
        OutStreamer.EmitInstruction(*I);

      return;
    }

    break;
  }
  default:
    break;
  }

  MCInstLowering.Lower(MI, TmpInst0);
  OutStreamer.EmitInstruction(TmpInst0);
}

//===----------------------------------------------------------------------===//
//
//  Cpu0 Asm Directives
//
//  -- Frame directive "frame Stackpointer, Stacksize, RARegister"
//  Describe the stack frame.
//
//  -- Mask directives "(f)mask  bitmask, offset"
//  Tells the assembler which registers are saved and where.
//  bitmask - contain a little endian bitset indicating which registers are
//            saved on function prologue (e.g. with a 0x80000000 mask, the
//            assembler knows the register 31 (RA) is saved at prologue.
//  offset  - the position before stack pointer subtraction indicating where
//            the first saved register on prologue is located. (e.g. with a
//
//  Consider the following function prologue:
//
//    .frame  $fp,48,$ra
//    .mask   0xc0000000,-8
//       addiu $sp, $sp, -48
//       st $ra, 40($sp)
//       st $fp, 36($sp)
//
//    With a 0xc0000000 mask, the assembler knows the register 31 (RA) and
//    30 (FP) are saved at prologue. As the save order on prologue is from
//    left to right, RA is saved first. A -8 offset means that after the
//    stack pointer subtration, the first register in the mask (RA) will be
//    saved at address 48-8=40.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Mask directives
//===----------------------------------------------------------------------===//
//	.frame	$sp,8,$lr
//->	.mask 	0x00000000,0
//	.set	noreorder
//	.set	nomacro

// Create a bitmask with all callee saved registers for CPU or Floating Point
// registers. For CPU registers consider RA, GP and FP for saving if necessary.
void Cpu0AsmPrinter::printSavedRegsBitmask(raw_ostream &O) {
  // CPU and FPU Saved Registers Bitmasks
  unsigned CPUBitmask = 0;
  int CPUTopSavedRegOff;

  // Set the CPU and FPU Bitmasks
  const MachineFrameInfo *MFI = MF->getFrameInfo();
  const std::vector<CalleeSavedInfo> &CSI = MFI->getCalleeSavedInfo();
  // size of stack area to which FP callee-saved regs are saved.
  unsigned CPURegSize = Cpu0::CPURegsRegClass.getSize();
  unsigned i = 0, e = CSI.size();

  // Set CPU Bitmask.
  for (; i != e; ++i) {
    unsigned Reg = CSI[i].getReg();
    unsigned RegNum = getCpu0RegisterNumbering(Reg);
    CPUBitmask |= (1 << RegNum);
  }

  CPUTopSavedRegOff = CPUBitmask ? -CPURegSize : 0;

  // Print CPUBitmask
  O << "\t.mask \t"; printHex32(CPUBitmask, O);
  O << ',' << CPUTopSavedRegOff << '\n';
}

// Print a 32 bit hex number with all numbers.
void Cpu0AsmPrinter::printHex32(unsigned Value, raw_ostream &O) {
  O << "0x";
  for (int i = 7; i >= 0; i--)
    O.write_hex((Value & (0xF << (i*4))) >> (i*4));
}

//===----------------------------------------------------------------------===//
// Frame and Set directives
//===----------------------------------------------------------------------===//
//->	.frame	$sp,8,$lr
//	.mask 	0x00000000,0
//	.set	noreorder
//	.set	nomacro
/// Frame Directive
void Cpu0AsmPrinter::emitFrameDirective() {
  const TargetRegisterInfo &RI = *TM.getRegisterInfo();

  unsigned stackReg  = RI.getFrameRegister(*MF);
  unsigned returnReg = RI.getRARegister();
  unsigned stackSize = MF->getFrameInfo()->getStackSize();

  if (OutStreamer.hasRawTextSupport())
    OutStreamer.EmitRawText("\t.frame\t$" +
           StringRef(Cpu0InstPrinter::getRegisterName(stackReg)).lower() +
           "," + Twine(stackSize) + ",$" +
           StringRef(Cpu0InstPrinter::getRegisterName(returnReg)).lower());
}

/// Emit Set directives.
const char *Cpu0AsmPrinter::getCurrentABIString() const {
  switch (Subtarget->getTargetABI()) {
  case Cpu0Subtarget::O32:  return "abi32";
  default: llvm_unreachable("Unknown Cpu0 ABI");;
  }
}

//		.type	main,@function
//->		.ent	main                    # @main
//	main:
void Cpu0AsmPrinter::EmitFunctionEntryLabel() {
  std::string ArgString;
  const Function::ArgumentListType &ArgList = MF->getFunction()->getArgumentList();
  bool First = true;
  for (Function::ArgumentListType::const_iterator iter = ArgList.begin();
       iter != ArgList.end(); iter++) {
    // TODO: probably need to map the frame indexes somehow
    if (!First) {
      ArgString += ", ";
    }
    First = false;
    ArgString += "a";
  }
  OS << "function " + Twine(CurrentFnSym->getName()) + "(" + ArgString + ") {\n";
}


//	.frame	$sp,8,$pc
//	.mask 	0x00000000,0
//->	.set	noreorder
//->	.set	nomacro
/// EmitFunctionBodyStart - Targets can override this to emit stuff before
/// the first basic block in the function.
void Cpu0AsmPrinter::EmitFunctionBodyStart() {
  MCInstLowering.Initialize(Mang, &MF->getContext());

  const TargetRegisterInfo &RI = *TM.getRegisterInfo();
  SparseSet<unsigned> SeenDefs;
  SeenDefs.setUniverse(RI.getNumRegs());

  for (MachineFunction::const_iterator I = MF->begin(), E = MF->end();
       I != E; ++I) {
    for (MachineBasicBlock::const_iterator II = I->begin(), IE = I->end();
         II != IE; ++II) {
      for (unsigned i = 0, e = II->getNumOperands(); i != e; ++i) {
        const MachineOperand &MO = II->getOperand(i);

        if (MO.isReg() && MO.isDef() &&
            TargetRegisterInfo::isPhysicalRegister(MO.getReg()) &&
            SeenDefs.insert(MO.getReg()).second) {
          // TODO: initialize differently depending on Int/FP register class.
          OS << "\tvar $" << Cpu0InstPrinter::getRegisterName(MO.getReg()) << " = 0;\n";
        }
      }
    }
  }

#if 0
  emitFrameDirective();
  bool EmitCPLoad = (MF->getTarget().getRelocationModel() == Reloc::PIC_) &&
    Cpu0FI->globalBaseRegSet() &&
    Cpu0FI->globalBaseRegFixed();

  if (OutStreamer.hasRawTextSupport()) {
    SmallString<128> Str;
    raw_svector_ostream OS(Str);
    printSavedRegsBitmask(OS);
    OutStreamer.EmitRawText(OS.str());
    OutStreamer.EmitRawText(StringRef("\t.set\tnoreorder"));
    // Emit .cpload directive if needed.
    if (EmitCPLoad)
      OutStreamer.EmitRawText(StringRef("\t.cpload\t$t9"));
    OutStreamer.EmitRawText(StringRef("\t.set\tnomacro"));
    if (Cpu0FI->getEmitNOAT())
      OutStreamer.EmitRawText(StringRef("\t.set\tnoat"));
  } else if (EmitCPLoad) {
    SmallVector<MCInst, 4> MCInsts;
    MCInstLowering.LowerCPLOAD(MCInsts);
    for (SmallVector<MCInst, 4>::iterator I = MCInsts.begin();
       I != MCInsts.end(); ++I)
      OutStreamer.EmitInstruction(*I);
  }
#endif
}

//->	.set	macro
//->	.set	reorder
//->	.end	main
/// EmitFunctionBodyEnd - Targets can override this to emit stuff after
/// the last basic block in the function.
void Cpu0AsmPrinter::EmitFunctionBodyEnd() {
#if 0
  // There are instruction for this macros, but they must
  // always be at the function end, and we can't emit and
  // break with BB logic.
  if (OutStreamer.hasRawTextSupport()) {
    if (Cpu0FI->getEmitNOAT())
      OutStreamer.EmitRawText(StringRef("\t.set\tat"));
    OutStreamer.EmitRawText(StringRef("\t.set\tmacro"));
    OutStreamer.EmitRawText(StringRef("\t.set\treorder"));
    OutStreamer.EmitRawText("\t.end\t" + Twine(CurrentFnSym->getName()));
  }
#endif
  OS << "}\n";
}

void Cpu0AsmPrinter::EmitStartOfAsmFile(Module &M) {
  // TODO replace this with a proper printf from existing emscripten library
  //      code
  OS << "function _printf(format, varargs) {\n"
        "\tvar view = new Int8Array(buffer);\n"
        "\tprint(view[format>>2], varargs);\n"
        "}\n"
        "var Math_min = Math.min;\n"
        "var buffer = new ArrayBuffer(4096);\n";
  OS << "var asm = (function(global, env, buffer) {\n"
        "\t'use asm';\n"
        "\tvar _printf = env._printf;\n"
        "\tvar HEAP8 = new global.Int8Array(buffer);\n"
        "\tvar HEAP16 = new global.Int16Array(buffer);\n"
        "\tvar HEAP32 = new global.Int32Array(buffer);\n"
        "\tvar HEAPU8 = new global.Uint8Array(buffer);\n"
        "\tvar HEAPU16 = new global.Uint16Array(buffer);\n"
        "\tvar HEAPU32 = new global.Uint32Array(buffer);\n"
        "\tvar HEAPF32 = new global.Float32Array(buffer);\n"
        "\tvar HEAPF64 = new global.Float64Array(buffer);\n";
}

void Cpu0AsmPrinter::EmitEndOfAsmFile(Module &M) {
  OS << "return { _main: _main };\n"
        "})\n"
        "({ 'Math': Math, 'Int8Array': Int8Array, 'Int16Array': Int16Array, 'Int32Array': Int32Array, 'Uint8Array': Uint8Array, 'Uint16Array': Uint16Array, 'Uint32Array': Uint32Array, 'Float32Array': Float32Array, 'Float64Array': Float64Array }, { 'min': Math_min, '_printf': _printf, 'NaN': NaN, 'Infinity': Infinity }, buffer);\n";
  OS <<
"var ALLOC_NONE = 0;\n"
"var Runtime = { GLOBAL_BASE: 0 };\n"
"function allocate(a, x, y, offset) {\n"
"  var view = new Int8Array(buffer);\n"
"  view.set(a, offset);\n"
"}\n";
  OS << "allocate([";
  for (std::vector<char>::iterator I = GlobalHeap.begin();
      I != GlobalHeap.end(); ++I) {
    if (I != GlobalHeap.begin()) {
      OS << ",";
    }
    OS << itostr(*I);
  }
  OS << "], 'i8', ALLOC_NONE, Runtime.GLOBAL_BASE);\n";
  OS << "asm._main();";
}

void Cpu0AsmPrinter::EmitGlobalVariable(const GlobalVariable *GV) {
  const Constant *CV = GV->getInitializer();
  if (const ConstantDataSequential *CDS = dyn_cast<ConstantDataSequential>(CV)) {
    if (CDS->isString()) {
      StringRef S = CDS->getAsString();
      for (const char* C = S.begin(); C != S.end(); ++C) {
        GlobalHeap.push_back(*C);
      }
      return;
    }
  }
  assert(false);
}

MachineLocation
Cpu0AsmPrinter::getDebugValueLocation(const MachineInstr *MI) const {
  // Handles frame addresses emitted in Cpu0InstrInfo::emitFrameIndexDebugValue.
  assert(MI->getNumOperands() == 4 && "Invalid no. of machine operands!");
  assert(MI->getOperand(0).isReg() && MI->getOperand(1).isImm() &&
         "Unexpected MachineOperand types");
  return MachineLocation(MI->getOperand(0).getReg(),
                         MI->getOperand(1).getImm());
}

void Cpu0AsmPrinter::PrintDebugValueComment(const MachineInstr *MI,
                                           raw_ostream &OS) {
  // TODO: implement
  OS << "PrintDebugValueComment()";
}

// Force static initialization.
extern "C" void LLVMInitializeCpu0AsmPrinter() {
  RegisterAsmPrinter<Cpu0AsmPrinter> X(TheCpu0Target);
  RegisterAsmPrinter<Cpu0AsmPrinter> Y(TheCpu0elTarget);
}
