
src = File.read('main.cpp')
rx = /NEON_ARGS_CHECKTYPE\s*\(\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\)/
src.gsub!(rx){|src|
  m=src.match(rx)
  first=m[1]
  sec=m[2]
  thi=m[3]
  prefix = 'Value::OBJTYPE_'
  base = thi.gsub(/^is/, '').downcase
  if %w(number bool).include?(base) then
    prefix = 'Value::VALTYPE_'
  end
  typ = prefix + base.upcase
  rs = sprintf("%s->checkType(%s, %s)", first, sec, typ)
  p rs
  rs
}

File.write('n.cpp', src)
