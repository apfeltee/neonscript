#!/usr/bin/ruby

LEVELS = 3

def gen_thing_actual(b, level)
  b.call
end

def gen_thing(&b)
  level = 0
  gen_thing_actual(b, level)
end

begin
  File.open("lexbreak.nn", "wb") do |ofh|
    ofh.print("print(\"")
    (0 .. 100000000).each do |i|
      ofh.printf("\\x%2X", i ^ 255)
    end
    ofh.print("\")")
  end
end



