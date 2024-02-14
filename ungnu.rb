#!/usr/bin/ruby

REGEXES = {
  /\b__attribute__\b\s*\(.+?\)/ => "",
  /\b__(inline|extension__|restrict)\b/ => "",
}

def ungnu(file)
  $stderr.printf("reading from %p ...\n", file)
  data = File.read(file)
  REGEXES.each do |rx, sub|
    data.gsub!(rx, sub)
  end
  $stderr.printf("now writing back to %p ...\n", file)
  File.write(file, data)
end

begin
  ARGV.each do |arg|
    ungnu(arg)
  end
end
