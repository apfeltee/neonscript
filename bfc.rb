

class OpCode
  MOVE = 1
  ADD = 2
  PRINTBYTE = 3
  READBYTE = 4
  JUMPIFFALSE = 5
  JUMPIFTRUE = 6
end

class Field
  Oper = 0
  Argument = 1
end

def makefilledarray(cnt, val)
  return ([val] * cnt)
end

class BFRunner
  def initialize()
    @compiledcode = []
    @compiledcount = 0
    @sourcepos = 0
  end

  def compile(program, len)
    while(@sourcepos < len) do
      c = program[@sourcepos]
      @sourcepos += 1
      if(c == '>' || c == '<') then
        if((@compiledcount > 0) && (@compiledcode[@compiledcount - 1][Field::Oper] == OpCode::MOVE)) then
          mval = -1
          if(c == '>') then
            mval = +1
          end
          @compiledcode[@compiledcount - 1][Field::Argument] += mval
        else
          pvop = OpCode::MOVE
          mval = -1
          if(c == '>') then
            mval = +1
          end
          @compiledcode.push([OpCode::MOVE, mval])
          @compiledcount += 1
        end
      elsif(c == '+' || c == '-') then
        if((@compiledcount > 0) && (@compiledcode[@compiledcount - 1][Field::Oper] == OpCode::ADD)) then
          mval = -1
          if(c == '+') then
              mval = +1
          end
          @compiledcode[@compiledcount - 1][Field::Argument] += mval
        else
          mval = -1
          if(c == '+') then
              mval = +1
          end
          @compiledcode.push([OpCode::ADD, mval])
          @compiledcount += 1
        end
      elsif(c == '.') then
        @compiledcode.push([OpCode::PRINTBYTE, 0])
        @compiledcount += 1
      elsif(c == ',') then
        @compiledcode.push([OpCode::READBYTE, 0])
        @compiledcount += 1
      elsif(c == '[') then
        @compiledcode.push([OpCode::JUMPIFFALSE, 0])
        @compiledcount += 1
      elsif(c == ']') then
        @compiledcode.push([OpCode::JUMPIFTRUE, 0])
        @compiledcount += 1
      end
    end
    print("compiled ", @compiledcount, " instructions. now resolving jumps...\n")
    i = 0
    while (i < @compiledcount) do
      tmpop = @compiledcode[i][Field::Oper]
      if(tmpop == OpCode::JUMPIFFALSE) then
        depth = 1
        target = i + 1
        while((depth > 0) && (target < @compiledcount)) do
          nextop = @compiledcode[target][Field::Oper]
          target += 1
          if(nextop == OpCode::JUMPIFFALSE) then
            depth += 1
          elsif(nextop == OpCode::JUMPIFTRUE) then
            depth -= 1
          end
        end
        if(depth > 0) then
          return false
        end
        @compiledcode[i][Field::Argument] = target
      elsif(tmpop == OpCode::JUMPIFTRUE) then
        depth = 1
        target = i
        while((depth > 0) && target >= 0) do
          target -= 1
          nextop = @compiledcode[target][Field::Oper]
          if(nextop == OpCode::JUMPIFTRUE) then
            depth += 1
          elsif(nextop == OpCode::JUMPIFFALSE) then
            depth -= 1
          end
        end
        if(depth > 0) then
          return false
        end
        @compiledcode[i][Field::Argument] = target + 1
      end
      i += 1
    end
    print("finished compiling!\n")
    return true
  end

  def run()
    maxcodecount = @compiledcode.length
    mem = makefilledarray(30000, 0)
    datapos = 0
    codepos = 0
    print("running ", maxcodecount, " instructions...\n")
    while(codepos < maxcodecount) do
      arg = @compiledcode[codepos][Field::Argument]
      opc = @compiledcode[codepos][Field::Oper]
      codepos += 1
      currmemlen = mem.length
      tmppos = datapos
      if(opc == OpCode::MOVE) then
        datapos = (datapos + arg + 30000) % 30000
      elsif(opc == OpCode::ADD) then
        mem[datapos] += arg
      elsif(opc == OpCode::JUMPIFFALSE) then
        if(mem[datapos] > 0) then
          codepos = codepos
        else
          codepos = arg
        end
      elsif(opc == OpCode::JUMPIFTRUE) then
        if(mem[datapos] > 0) then
          codepos = arg
        else
          codepos = codepos
        end
      elsif(opc == OpCode::PRINTBYTE) then
        $stdout.write(mem[datapos].chr)
        $stdout.flush
      elsif(opc == OpCode::READBYTE) then
        $stdout.flush()
        c = $stdin.getc
        if(c == nil) then
          mem[datapos] = 0
        else
          mem[datapos] = c.ord
        end
      end
    end
  end

  def optoname(op)
    if(op == OpCode::MOVE) then
      return "move"
    elsif(op == OpCode::ADD) then
      return "add"
    elsif(op == OpCode::PRINTBYTE) then
      return "printbyte"
    elsif(op == OpCode::READBYTE) then
      return "readbyte"
    elsif(op == OpCode::JUMPIFFALSE) then
      return "jumpiffalse"
    elsif(op == OpCode::JUMPIFTRUE) then
      return "jumpifnotzero"
    end
    return nil
  end

  def printbc()
    brline = 0
    len = @compiledcode.length
    print("compiled: ", len, " items: [\n")
    i = 0
    while (i<len) do
      nopt = @compiledcode[i][Field::Oper]
      arg = @compiledcode[i][Field::Argument]
      optn = optoname(nopt)
      print("", optn, "(", arg, ")")
      if((i+1) < len) then
        print(", ")
      end
      if(brline == 10) then
        print("\n")
        brline = 0
      end
      brline += 1
      i += 1
    end
    print("\n]\n")
  end
end

def getargv(i)
  return ARGV[i]
end

def readfile(path)
  return File.read(path)
end

def main()
  src = "++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++."
  bfr = BFRunner.new()
  if ARGV.length > 0 then
    fpath = ARGV[0]
    print("fpath=", fpath, "\n")
    src = readfile(fpath)
  end
  if(!bfr.compile(src, src.length)) then
    print("failed to compile program\n")
  end
  bfr.run()
end

main()
