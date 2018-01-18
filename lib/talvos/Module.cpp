// Copyright (c) 2018 the Talvos developers. All rights reserved.
//
// This file is distributed under a three-clause BSD license. For full license
// terms please see the LICENSE file distributed with this source code.

#include <cassert>
#include <cstdio>
#include <iostream>
#include <spirv-tools/libspirv.hpp>

#include <spirv/unified1/spirv.h>

#include "talvos/Module.h"

namespace talvos
{

class ModuleBuilder
{
public:
  void init(uint32_t IdBound)
  {
    assert(!Mod && "Module already initialized");
    Mod = std::unique_ptr<Module>(new Module(IdBound));
    CurrentFunction = nullptr;
    PreviousInstruction = nullptr;
  }

  void processInstruction(const spv_parsed_instruction_t *Inst)
  {
    assert(Mod);

    if (Inst->opcode == SpvOpFunction)
    {
      assert(CurrentFunction == nullptr);
      // TODO: Cleanup - when is this destroyed?
      // Should be owned by Module? Module::createFunction()?
      CurrentFunction = new Function;
      CurrentFunction->FirstInstruction = nullptr;
    }
    else if (Inst->opcode == SpvOpFunctionEnd)
    {
      assert(CurrentFunction);
      Mod->addFunction(CurrentFunction);
      CurrentFunction = nullptr;
      PreviousInstruction = nullptr;
    }
    else if (CurrentFunction)
    {
      // TODO: Cleanup - when is this destroyed, by parent Function?
      Instruction *I = new Instruction;
      I->Opcode = Inst->opcode;
      I->NumOperands = Inst->num_operands;
      // TODO: Are all operands IDs?
      I->Operands = new uint32_t[I->NumOperands];
      for (int i = 0; i < Inst->num_operands; i++)
      {
        // TODO: Handle larger operands
        assert(Inst->operands[i].num_words == 1);
        I->Operands[i] = Inst->words[Inst->operands[i].offset];
      }
      I->Next = nullptr;
      if (!CurrentFunction->FirstInstruction)
        CurrentFunction->FirstInstruction = I;
      else if (PreviousInstruction)
        PreviousInstruction->Next = I;
      PreviousInstruction = I;
    }
    else
    {
      switch (Inst->opcode)
      {
      case SpvOpConstant:
      {
        // TODO: Use actual type
        uint32_t Value = Inst->words[Inst->operands[2].offset];
        Mod->addObject(Inst->result_id, Object::create<uint32_t>(Value));
        break;
      }
      case SpvOpVariable:
        Mod->addVariable(Inst->result_id,
                         Inst->words[Inst->operands[2].offset]);
        break;
      default:
        std::cout << "Unhandled opcode " << Inst->opcode << std::endl;
      }
    }
  };

  std::unique_ptr<Module> takeModule()
  {
    assert(Mod);
    return std::move(Mod);
  }

private:
  std::unique_ptr<Module> Mod;
  Function *CurrentFunction;
  Instruction *PreviousInstruction;
};

spv_result_t HandleHeader(void *user_data, spv_endianness_t endian,
                          uint32_t /* magic */, uint32_t version,
                          uint32_t generator, uint32_t id_bound,
                          uint32_t schema)
{
  ((ModuleBuilder *)user_data)->init(id_bound);
  return SPV_SUCCESS;
}

spv_result_t
HandleInstruction(void *user_data,
                  const spv_parsed_instruction_t *parsed_instruction)
{
  ((ModuleBuilder *)user_data)->processInstruction(parsed_instruction);
  return SPV_SUCCESS;
}

Module::Module(uint32_t IdBound)
{
  this->IdBound = IdBound;
  this->Objects.resize(IdBound);
}

Module::~Module()
{
  for (Object &Obj : Objects)
    Obj.destroy();
}

void Module::addFunction(Function *Func)
{
  // TODO: Support multiple functions
  this->Func = Func;
}

void Module::addObject(uint32_t Id, const Object &Obj)
{
  assert(Id < Objects.size());
  Objects[Id] = Obj;
}

void Module::addVariable(uint32_t Id, uint32_t StorageClass)
{
  assert(Variables.count(Id) == 0);

  Variable V;
  // TODO: Type, initializers etc
  Variables.insert({Id, V});
}

std::vector<Object> Module::cloneObjects() const
{
  std::vector<Object> ClonedObjects;
  ClonedObjects.resize(Objects.size());
  for (int i = 0; i < Objects.size(); i++)
  {
    if (Objects[i].isSet())
      ClonedObjects[i] = Objects[i].clone();
  }
  return ClonedObjects;
}

Function *Module::getFunction() const
{
  // TODO: Support multiple functions
  return this->Func;
}

std::unique_ptr<Module> Module::load(const std::string &FileName)
{
  // Open file.
  FILE *SPVFile = fopen(FileName.c_str(), "rb");
  if (!SPVFile)
  {
    std::cerr << "Failed to open '" << FileName << "'" << std::endl;
    return nullptr;
  }

  // Read file data.
  fseek(SPVFile, 0, SEEK_END);
  long NumBytes = ftell(SPVFile);
  uint32_t Words[NumBytes];
  fseek(SPVFile, 0, SEEK_SET);
  fread(Words, 1, NumBytes, SPVFile);
  fclose(SPVFile);

  ModuleBuilder MB;

  // Parse binary.
  spv_diagnostic diagnostic = nullptr;
  spvtools::Context SPVContext(SPV_ENV_UNIVERSAL_1_2);
  spvBinaryParse(SPVContext.CContext(), &MB, Words, NumBytes / 4, HandleHeader,
                 HandleInstruction, &diagnostic);
  if (diagnostic)
  {
    spvDiagnosticPrint(diagnostic);
    return nullptr;
  }

  return MB.takeModule();
}

} // namespace talvos
