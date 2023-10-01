class Tree
  def initialize(item, depth)
    @item = item
    @depth = depth
    if (depth > 0) then
      item2 = item + item
      depth = depth - 1
      @left = Tree.new(item2 - 1, depth)
      @right = Tree.new(item2, depth)
    else
      @left = nil
      @right = nil
    end
  end

  def check()
    if (@left == nil) then
      return @item
    end
    return @item + @left.check() - @right.check()
  end
end

mindepth = 4
maxdepth = 8
stretchdepth = maxdepth + 1

print("stretch tree of depth: ", stretchdepth, "; check: ", Tree.new(0, stretchdepth).check(), "\n");

longlivedtree = Tree.new(0, maxdepth);

iterations = 1;
d = 0;
while (d < maxdepth) do
  iterations = iterations * 2;
  d = d + 1;
end

depth = mindepth;
while (depth < stretchdepth) do
  check = 0;
  i = 1;
  while (i <= iterations) do
    check = check + Tree.new(i, depth).check() + Tree.new(-i, depth).check()
    i = i + 1
  end

  print("num trees:", iterations * 2, "; depth:", depth, "; check:", check, "\n")

  iterations = iterations / 4
  depth = depth + 2
end

print("long lived tree of depth:", maxdepth, "; check:", longlivedtree.check(), "\n")
