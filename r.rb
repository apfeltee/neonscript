src=<<__eos__
NEON_ARGS_CHECKTYPE(args, 0, &Value::isCallable);
NEON_ARGS_CHECKTYPE(args, 0, &Value::isString);
NEON_ARGS_CHECKTYPE(args, 1, &Value::isNumber);
NEON_ARGS_CHECKTYPE(args, 0, &Value::isArray);
NEON_ARGS_CHECKTYPE(args, 0, &Value::isBool);
NEON_ARGS_CHECKTYPE(args, 0, &Value::isInstance);
NEON_ARGS_CHECKTYPE(args, 1, &Value::isClass);

__eos__
src.split(';').map(&:strip).reject(&:empty?).each do |line|
  m = line.match(/\&Value::(?<name>\w+)/)
  name = m['name']
  printf("else if(fn == &Value::%s)\n", name)
  printf("{\n")
  printf("    return %p;\n", name[2 .. -1].downcase)
  printf("}\n")
end
