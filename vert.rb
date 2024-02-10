#!/usr/bin/ruby

# print type sizes side-by-side, for verification.


def parse(plain)
  dt = {}
  plain.strip.each_line do |l|
    l.strip!
    next if l.empty?
    b = l.split(/\t/)
    sz = b[0]
    tn = b[1]
    dt[tn] = sz.to_i
  end
  return dt
end

begin
  mapping =
  {
    "neon::Printer" => "NeonPrinter",
    "neon::Value" => "NeonValue",
    "neon::Object" => "NeonObject",
    "neon::Property::GetSetter" => "NeonPropGetSet",
    "neon::Property" => "NeonProperty",
    "neon::ValArray" => "NeonValArray",
    "neon::Blob" => "NeonBlob",
    "neon::HashTable::Entry" => "NeonHashEntry",
    "neon::HashTable" => "NeonHashTable",
    "neon::String" => "NeonObjString",
    "neon::ScopeUpvalue" => "NeonObjUpvalue",
    "neon::Module" => "NeonObjModule",
    "neon::FuncScript" => "NeonObjFuncScript",
    "neon::FuncClosure" => "NeonObjFuncClosure",
    "neon::ClassObject" => "NeonObjClass",
    "neon::ClassInstance" => "NeonObjInstance",
    "neon::FuncBound" => "NeonObjFuncBound",
    "neon::FuncNative" => "NeonObjFuncNative",
    "neon::Array" => "NeonObjArray",
    "neon::Range" => "NeonObjRange",
    "neon::Dictionary" => "NeonObjDict",
    "neon::File" => "NeonObjFile",
    "neon::VarSwitch" => "NeonObjSwitch",
    "neon::Userdata" => "NeonObjUserdata",
    "neon::State::ExceptionFrame" => "NeonExceptionFrame",
    "neon::State::CallFrame" => "NeonCallFrame",
    "neon::State" => "NeonState",
    "neon::Token" => "NeonAstToken",
    "neon::Lexer" => "NeonAstLexer",
    "neon::Parser::CompiledLocal" => "NeonAstLocal",
    "neon::Parser::CompiledUpvalue" => "NeonAstUpvalue",
    "neon::Parser::FuncCompiler" => "NeonAstFuncCompiler",
    "neon::Parser::ClassCompiler" => "NeonAstClassCompiler",
    "neon::Parser" => "NeonAstParser",
    "neon::Parser::Rule" => "NeonAstRule",
    "neon::RegModule::FuncInfo" => "NeonRegFunc",
    "neon::RegModule::FieldInfo" => "NeonRegField",
    "neon::RegModule::ClassInfo" => "NeonRegClass",
    "neon::RegModule" => "NeonRegModule",
  }

  cmdold = parse(%x{../run -t})
  cmdcpp = parse(%x{./run -t})
  res = []
  cmdcpp.each do |tn, sz|
    oldk = mapping[tn]
    if oldk == nil then
      $stderr.printf("!!!!!!!!!!!! cannot find mapping for %p\n", tn)
    else
      res.push([[oldk, cmdold[oldk]], [tn, sz]])
    end
  end
  res.sort_by!{|itm| (itm[0][1] + itm[1][1]) }
  res.each do |itm|
    olditm = itm[0]
    cppitm = itm[1]
    oldnm = olditm[0]
    cppnm = cppitm[0]
    oldsz = olditm[1]
    cppsz = cppitm[1]
    printf("%s: %d / %s: %d\n", oldnm, oldsz, cppnm, cppsz)
  end

end



