#!/usr/bin/ruby

# dirty little lexer.
# currently serves to brainstorm parsing; eventually might replace the 
# adhoc parser with a proper syntax tree (in C, not Ruby, obviously).

class Token
  attr_reader :name, :value, :line, :column
  def initialize(name, str, line, column)
    @name = name
    @value = str
    @line = line
    @column = column
  end
end

class Tokenizer
  def initialize(src)
    @src = src
    @maxlen = src.length
    @pos = 0
    @dbgline = 1
    @dbgcolumn = 0
    @currch = nil
    @nextch = nil
    @nextnextch = nil
    @callback = nil
    @result = []
  end

  def error(msg)
    $stderr.printf("error: near line %d, column %d: %s\n", @dbgline, @dbgcolumn, msg)
    raise
  end

  def advance()
    @currch = @src[@pos]
    if @currch == "\n" then
      @dbgline += 1
      @dbgcolumn = 0
    end
    @nextch = @src[@pos + 1]
    @dbgcolumn += 1
    if @pos == @maxlen then
      return nil
    end
    @pos += 1
  end

  def push(name, str)
    tok = Token.new(name, str, @dbgline, @dbgcolumn)
    if @callback == nil then
      @result.push(tok)
    else
      @callback.call(tok)
    end
  end

  def isdigit(ch)
    c = ch.ord
    return (c >= '0'.ord) && (c <= '9'.ord);
  end

  def isalpha(ch)
    c = ch.ord
    return (((c >= 'a'.ord) && (c <= 'z'.ord)) || ((c >= 'A'.ord) && (c <= 'Z'.ord)) || (c == '_'.ord));
  end

  def isspace(ch)
    c = ch.ord
    return ((c == " ".ord) || (c == "\n".ord) || (c == "\r".ord) || (c == "\t".ord))
  end

  def scancommentline()
    buf = ""
    buf += @currch
    while true do
      if @nextch == "\n" then
        break
      end
      advance()
      buf += @currch
    end
    push(:commentline, buf)
  end

  def scancommentblock()
    buf = ""
    # eat '/'
    advance()
    # eat '*'
    advance()
    buf += @currch
    while true do
      advance()
      if @currch == '*' then
        if @nextch == '/' then
          advance()
          break
        end
      end
      buf += @currch
    end
    push(:commentblock, buf)
  end

  def scannumber()
    buf = ""
    buf += @currch
    while true do
      if !isdigit(@nextch) then
        break
      end
      advance()
      buf += @currch
    end
    push(:digit, buf)
  end

  def scanstring(name, endch)
    buf = ""
    $stderr.printf("starting string scan (%p) at line %d, column %d ...\n", endch, @dbgline, @dbgcolumn)
    while true do
      if @nextch == endch then
        advance()
        break
      end
      advance()
      if @currch == "\\" then
        if @nextch == "\\" then
          buf += "\\"
          advance()
        else
          nch = @nextch
          advance()
          # i.e., `"foo\"bar"` --> `foo"bar`, etc
          if nch == endch then
            buf += endch
          # but also handle `'` and `"` explicitly, in case endch != those ones
          elsif nch == "'" then
            buf += "'"
          elsif nch == "\"" then
            buf += "\""
          elsif nch == "n" then
            buf += "\n"
          elsif nch == "r" then
            buf += "\r"
          elsif nch == "t" then
            buf += "\t"
          elsif nch == "e" then
            buf += "\e"
          elsif nch == "a" then
            buf += "\a"
          elsif nch == "b" then
            buf += "\b"
          elsif nch == "f" then
            buf += "\f"
          elsif nch == "v" then
            buf += "\v"
          elsif nch == "0" then
            buf += "\0"
          else
            #error("invalid escape sequence '\\#{nch}'")
            buf += nch
          end
        end
      else
        buf += @currch
      end
    end
    $stderr.printf("finished string scanning at line %d, column %d\n", @dbgline, @dbgcolumn)
    push(name, buf)
  end

  def scanident()
    buf = ""
    buf += @currch
    while true do
      if !isalpha(@nextch) then
        break
      end
      advance()
      buf += @currch
    end
    push(:ident, buf)
  end

  def scanoperator()
    if isspace(@currch) then
      if @currch == "\n" then
        push(:spacelinefeed, @currch)
      elsif @currch == "\t" then
        push(:spacetab, @currch)
      elsif @currch == " " then
        push(:spacespace, " ")
      end
    else
      savech = @currch
      savenext = @nextch
      if (savech == '/') && (savenext == '*') then
        scancommentblock()
      elsif (savech == '/') && (savenext == '/') then
        scancommentline()
      else
        if (savech == '+') || (savech == '-') || (savech == '*') || (savech == '/') || (savech == '%') then
          if savenext == savech then
            advance()
            if savenext == '+' then
              push(:opincrement, "++")
            elsif savenext == '-' then
              push(:opdecrement, "--")
            elsif savenext == '%' then
              error("invalid operator '%%'")
            elsif savenext == '*' then
              push(:oppowerof, "**")
            end
          elsif @nextch == '=' then
            if savech == '+' then
              push(:opassignplus, "+=")
            elsif savech == '-' then
              push(:opassignminus, "-=")
            elsif savech == '*' then
              push(:opassignmult, "*=")
            elsif savech == '/' then
              push(:opassigndiv, "/=")
            elsif savech == '%' then
              push(:opassignmodulo, "%=")
            end
          else
            if savech == '+' then
              push(:opplus, "+")
            elsif savech == '-' then
              push(:opminus, "-")
            elsif savech == '*' then
              push(:opmultiply, "*")
            elsif savech == '/' then
              push(:opdivide, "/")
            elsif savech == '%' then
              push(:opmodulo, "%")
            end
          end
        else
          push(:operator, @currch)
        end
      end
    end
  end

  def run(&block)
    @callback = block
    while true do
      advance()
      if (@currch == nil) then
        break
      end
      #$stderr.printf("currch=%p maxlen=%p pos=%p\n", @currch, @maxlen, @pos)
      if isdigit(@currch)
        scannumber()
      elsif isalpha(@currch)
        scanident()
      elsif @currch == '"' then
        scanstring(:stringfull, '"')
      elsif @currch == "'" then
        scanstring(:stringchar, "'")
      else
        scanoperator()
      end
    end
    return @result
  end
end

class AstExpression
  def initialize(type)
    @type = type
  end
end

class AstNodeIf < AstExpression
  def initialize()
    super(:nodeif)
  end
end

class Parser
  def initialize(lexer)
    @lexer = lexer
    @tokens = lexer.run()
    @pos = 0
  end

  def error_attoken(tok, msg)
    $stderr.printf("ERROR: near #{tok.line}:#{tok.column}: #{msg}\n")
    raise
  end

  def advance()
    @prevtoken = @currtoken
    @currtoken = @tokens[@pos]
    @nexttoken = @tokens[@pos + 1]
  end

  def run
    advance()
    case @currtoken.name
      when :ident
        
      else
        error_attoken(@currtoken, "unhandled token #{@currtoken.name} (#{@currtoken.value.dump})")
    end
  end
end

def getsource()
  if ARGV.empty? then
    return $stdin.read
  end
  return File.read(ARGV[0])
end


def restore(lexer)
  # "restore" the source by printing what we parsed.
  # ideally, should completely replicate input, if tokenized correctly.
  osource = []
  lexer.run() do |op|
    n = op.name
    ns = n.to_s
    str = op.value
    if !ns.match(/^space/) then
      $stderr.printf("-- %s -> %p\n", n, str)
    end
    if n.match?(/^space/) then
      osource.push(str)
    else
      if (n == :stringfull) || (n == :stringchar) then
        dumped = str.dump
        dumped = dumped[1 .. dumped.length - 2]
        if n == :stringchar then
          osource.push("'", dumped, "'")
        else
          osource.push("\"", dumped, "\"")
        end
      else
        osource.push(str)
      end
    end
  end
  osource.each do |s|
    print(s)
  end
end

begin
  lexer = Tokenizer.new(getsource())
  p = Parser.new(lexer)
  p.run()
end
