#!/usr/bin/ruby

module CType
  def self.isprint(b)
    return ((b >= 040) && (b < 0177))
  end

  def self.isdigit(b)
    return ((b <= '9'.ord) && (b >= '0'.ord))
  end

  def self.isspace(b)
    return ((b == 32) || (b == 9) || (b == 13) || (b == 10) || (b == 12))
  end

  def self.isascii(b)
    return ((b < 0200) && (b >= 0))
  end

  def self.isupper(c)
    return ((c <= 'Z'.ord) && (c >= 'A'.ord))
  end

  def self.islower(c)
    return ((c <= 'z'.ord) && ( c >= 'a'.ord))
  end

  def self.isalpha(c)
    return (isupper(c) || islower(c))
  end

  def self.isalnum(c)
    return (isalpha(c) || isdigit(c))
  end


  #define iscntrl(c) (((c) < 040 || (c) == 0177) && isascii((c)))
  #define ispunct(c) (!isalnum((c)) && isprint(c))
  #define toupper(c) (islower((c)) ? (c) & ~32 : (c))
  #define tolower(c) (isupper((c)) ? (c) | 32 : (c))
  #define toascii(c) ((c) & 0177)

end


def readnfromhandle(fh, needed, &block)
  rt = []
  isblock = block_given?
  cnt = 0
  fh.each_byte do |b|
    if cnt == needed then
      break
    end
    if CType.isalnum(b) then
      cnt += 1
      if isblock then
        block.call(b)
      else
        rt.push(b)
      end
    end
  end
  return rt
end

def readnfromfile(file, cnt, &b) 
  File.open(file, "rb") do |fh|
    return readnfromhandle(fh, cnt, &b)
  end
end

begin
  len = ((1024*1024)*64)
  #len = 10
  print('longstr = "')
  readnfromfile("/dev/urandom", len) do |b|
    print(b.chr)
  end
  print('"')
  print("\n")
  print('print(longstr.length, "\n")')
end

