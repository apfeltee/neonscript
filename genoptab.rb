
def readitems(lines, enumname, matchprefix)
  rt = []
  maxcount = lines.length
  i = 0
  while i < maxcount do
    line = lines[i]
    i += 1
    if line.match?(/^\s*\benum\b\s+\b#{enumname}\b/) then
      # skip this line, and the opening brace
      i += 2
      while i < maxcount do
        line = lines[i]
        i += 1
        if line.match?(/^\s*\}\s*;/) then
          break
        else
          m = line.match(/^\s*\b(?<item>#{matchprefix}\w+)\b(\s*=\s*(?<value>.*),?)?/)
          if m then
            name = m["item"]
            rt.push(name)
          end
        end
      end
    end
  end
  return rt
end

begin
  items = []
  lines = File.readlines("main.c")
  items = readitems(lines, "NeonOpCode", "NEON_OP_")
  items.each do |itm|
    printf("&&vmmac_opinstname(%s),\n", itm)
  end
end


