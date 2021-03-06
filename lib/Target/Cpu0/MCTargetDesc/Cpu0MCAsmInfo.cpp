//===-- Cpu0MCAsmInfo.cpp - Cpu0 Asm Properties ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the Cpu0MCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "Cpu0MCAsmInfo.h"
#include "llvm/ADT/Triple.h"

using namespace llvm;

void Cpu0MCAsmInfo::anchor() { }

Cpu0MCAsmInfo::Cpu0MCAsmInfo(const Target &T, StringRef TT) {
  Triple TheTriple(TT);
  if ((TheTriple.getArch() == Triple::cpu0))
    IsLittleEndian = false;

  AlignmentIsInBytes          = false;
  Data16bitsDirective         = "\t.2byte\t";
  Data32bitsDirective         = "\t.4byte\t";
  Data64bitsDirective         = "\t.8byte\t";
  GlobalPrefix                = "_";
  PrivateGlobalPrefix         = "$";
  CommentString               = "#";
  ZeroDirective               = "\t.space\t";
  GPRel32Directive            = "\t.gpword\t";
  GPRel64Directive            = "\t.gpdword\t";
  WeakRefDirective            = "\t.weak\t";
  CommentString               = "//";

  SupportsDebugInformation = true;
  ExceptionsType = ExceptionHandling::DwarfCFI;
  HasLEB128 = true;
  DwarfRegNumForCFI = true;
}
