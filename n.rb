#!/usr/bin/ruby

$sources = {}

def initsources()
  ["main.cpp", "optparse.h", "os.h", "strbuf.h"].each do |file|
    $sources[file] = File.read(file)
  end
end

def insources(name)
  ns = name.scrub
  $sources.each do |file, src|
    if src.match?(/\b#{ns}\b/) then
      return true
    end
  end
  return false
end

begin
  initsources()
  lines = []
  oldlines = []
  File.foreach("prot.inc") do |line|
    oldlines.push(line.strip)
  end
  oldlines.each do |line|
    m = line.match(/\b(?<n>\w+)\b\s*\(/)
    next unless m
    n = m["n"]
    if insources(n) then
      lines.push(line)
    end
  end
  $stderr.printf("old line count: %d, new line count: %d\n", oldlines.length, lines.length)
  if lines.length != oldlines.length then
    File.open("prot.inc", "wb") do |ofh|
      lines.each do |line|
        ofh.print(line, "\n")
      end
    end
  end
end

