class Tree {
  constructor(item, depth) {
    this.item = item
    this.depth = depth
    if (depth > 0) {
      var item2 = item + item
      depth = depth - 1
      this.left = new Tree(item2 - 1, depth)
      this.right = new Tree(item2, depth)
    } else {
      this.left = null
      this.right = null
    }
  }

  check() {
    if (this.left == null) {
      return this.item
    }

    return this.item + this.left.check() - this.right.check()
  }
}

var mindepth = 4
var maxdepth = 8
var stretchdepth = maxdepth + 1

console.log("stretch tree of depth: ", stretchdepth, "; check: ", (new Tree(0, stretchdepth)).check());

var longlivedtree = new Tree(0, maxdepth);

// iterations = 2 ** maxdepth
var iterations = 1;
var d = 0;
while (d < maxdepth) {
  iterations = iterations * 2;
  d = d + 1;
}

var depth = mindepth;
while (depth < stretchdepth) {
  var check = 0;
  var i = 1;
  while (i <= iterations) {
    check = check + (new Tree(i, depth)).check() + (new Tree(-i, depth)).check()
    i = i + 1
  }

  console.log("num trees:", iterations * 2, "; depth:", depth, "; check:", check)

  iterations = iterations / 4
  depth = depth + 2
}

console.log("long lived tree of depth:", maxdepth, "; check:", longlivedtree.check())
