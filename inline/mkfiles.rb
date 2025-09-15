#!/usr/bin/ruby

ATTRIBUTE = "__attribute__((hot, optimize(5)))"

begin
  cannotinline = %w(
    main
    nn_astfunccompiler_resolveupvalue
    nn_strutil_inpreplhelper
    nn_value_compare_actual
    nn_class_getstaticproperty
    nn_class_getmethodfield
    nn_printer_printvalue
    nn_value_sortvalues
    nn_object_makearray
    nn_vm_callvaluewithobject
    nn_gcmem_sweep
    nn_vmutil_modulo
  )
  File.open("inlprot.inc", "wb") do |ofh|
    File.foreach("../prot.inc") do |line|
      m = line.match(/^(?<def>\w+.*\b(?<name>\w+)\b\s*\(.*\))\s*;/)
      if m then
        fndef = m["def"]
        name = m["name"]
        if (fndef.match?(/\.\.\./) || fndef.match?(/va_list/)) || cannotinline.include?(name) then
          ofh.print(line)
        else
          ofh.printf("static inline %s %s;\n", ATTRIBUTE, fndef)
        end
      end
    end
  end

  File.open("neon.h", "wb") do |ofh|
    File.foreach("../neon.h") do |line|
      if line.match?(/^\s*#\s*include\s*"prot.inc"/) then
        ofh.printf("#include \"inlprot.inc\"\n")
      else
        ofh.print(line)
      end
    end
  end
  hfiles = []
  cfiles = Dir.glob("../*.c")
  File.open("all.c", "wb") do |ofh|
    ofh.printf("#include \"neon.h\"\n")
    (hfiles | cfiles).each do |file|
      ofh.printf("#include \"%s\"\n", file)
    end
  end
end

