

function A(k,x1,x2,x3,x4,x5)
{
    function inner()
    {
        k = k - 1
        return A(k,b,x1,x2,x3,x4);
    }
    b = inner
    if(k <= 0)
    {
        return x4() + x5();
    }
    return b();
}  

//console.log(a(10, x(1), x(-1), x(-1), x(1), x(0)));

function fna1() { return  1; }
function fna2() { return -1; }
function fna3() { return -1; }
function fna4() { return  1; }
function fna5() { return  0; }

println(A(10, fna1, fna2, fna3, fna4, fna5))