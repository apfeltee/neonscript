

function unshiftright(a, b)
{
    var na = a;
    var nb = b;
    if(((nb > 32) || (nb == 32)) || (nb < (0-32)))
    {
        m = (nb / 32);
        nb = nb - (m * 32);
    }
    if(nb < 0)
    {
        nb = 32 + nb;
    }
    if (nb == 0)
    {
        return ((na >> 1) & 0x7fffffff) * 2 + ((na >> nb) & 1);
    }
    if (na < 0) 
    { 
        na = (na >> 1); 
        na = na & 0x7fffffff; 
        na = na | 0x40000000; 
        na = (na >> (nb - 1)); 
    }
    else
    {
        na = (na >> nb); 
    }
    return na; 
}


var K_HEXCASE = 0;


var K_CHARSIZE   = 8;


function rol(num, cnt)
{
    return (num << cnt) | unshiftright(num, (32 - cnt));
}



function safe_add(x, y)
{
    if(x == null)
    {
        x = 0;
    }
    if(y == null)
    {
        y = 0;
    }
    var lsw = (x & 0xFFFF) + (y & 0xFFFF);
    var msw = (x >> 16) + (y >> 16) + (lsw >> 16);
    return (msw << 16) | (lsw & 0xFFFF);
}


function sha1_ft(t, b, c, d)
{
    if(t < 20)
    {
        return (b & c) | ((~b) & d);
    }
    if(t < 40)
    {
        return ((b ^ c) ^ d);
    }
    if(t < 60)
    {
        return (b & c) | (b & d) | (c & d);
    }
    return ((b ^ c) ^ d);
}



function sha1_kt(t)
{
    if(t < 20)
    {
        return 1518500249;
    }
    if(t < 40)
    {
        return 1859775393;
    }
    if(t < 60)
    {
        return -1894007588;
    }
    return -899497514;
}


function core_sha1(x, l)
{
    var aidx;
    aidx = (l >> 5);
    while(aidx >= x.length)
    {
        x.push(0);
    }
    x[aidx] = x[aidx] | 0x80 << (24 - l % 32);
    aidx = ((l + 64 >> 9) << 4) + 15;
    while(aidx >= x.length)
    {
        x.push(0);
    }
    x[aidx] = l;
    var w = [];
    var a =  1732584193;
    var b = -271733879;
    var c = -1732584194;
    var d =  271733878;
    var e = -1009589776;
    var i = 0;
    for(i = 0; i < x.length; i = i + 16)
    {
        var olda = a;
        var oldb = b;
        var oldc = c;
        var oldd = d;
        var olde = e;
        for(var j = 0; j < 80; j++)
        {
            if(j < 16)
            {
                while(j >= w.length)
                {
                    w.push(0);
                }
                w[j] = x[i + j];
            }
            else
            {
                var wv1 = w[j - 3];
                var wv2 = w[j - 8];
                var wv3 = w[j - 14];
                var wv4 = w[j - 16];
                if(wv1 == null)
                {
                    wv1 = 0;
                }
                if(wv2 == null)
                {
                    wv2 = 0;
                }
                if(wv3 == null)
                {
                    wv3 = 0;
                }
                if(wv4 == null)
                {
                    wv4 = 0;
                }
                while(j >= w.length)
                {
                    w.push(0);
                }
                w[j] = rol(((wv1 ^ wv2) ^ wv3) ^ wv4, 1);
            }
            var t = safe_add(safe_add(rol(a, 5), sha1_ft(j, b, c, d)), safe_add(safe_add(e, w[j]), sha1_kt(j)));
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = t;
        }
        a = safe_add(a, olda);
        b = safe_add(b, oldb);
        c = safe_add(c, oldc);
        d = safe_add(d, oldd);
        e = safe_add(e, olde);
    }
    var rt = [a, b, c, d, e];
    
    return rt;
}



function str2binb(str)
{
    var aidx;
    var bin = [];
    var mask = (1 << K_CHARSIZE) - 1;
    var len = str.length;
    
    for(var i = 0; i < len * K_CHARSIZE; i += K_CHARSIZE)
    {
        aidx = (i >> 5);
        if(aidx >= bin.length)
        {
            bin.push(0);
        }
        bin[aidx] = bin[aidx] | (str.charCodeAt(i / K_CHARSIZE) & mask) << (24 - i%32); 

    }
    return bin;
}

function core_hmac_sha1(key, data)
{
    var bkey = str2binb(key);
    if(bkey.length > 16)
    {
        bkey = core_sha1(bkey, key.length * K_CHARSIZE);
    }
    var ipad = [];
    var opad = [];
    for(var i = 0; i < 16; i++)
    {
        ipad[i] = bkey[i] ^ 0x36363636;
        opad[i] = bkey[i] ^ 0x5C5C5C5C;
    }
    var hash = core_sha1(ipad + str2binb(data), 512 + data.length * K_CHARSIZE);
    return core_sha1(opad + hash, 512 + 160);
}


function binb2hex(binarray)
{
    var hex_tab = "0123456789abcdef";
    var str = "";
    for(var i = 0; i < binarray.length * 4; i++)
    {
        var c1 = hex_tab[(binarray[i>>2] >> ((3 - i%4)*8+4)) & 0xF];
        var c2 = hex_tab[(binarray[i>>2] >> ((3 - i%4)*8  )) & 0xF];
        str += c1;
        str += c2;
    }
    return str;
}



function hex_sha1(s)
{
    return binb2hex(core_sha1(str2binb(s),s.length * K_CHARSIZE));
}

var demo = [
    ["foo", "0beec7b5ea3f0fdbc95d0dd47f3c5bc275da8a33"],
    ["bar", "62cdb7020ff920e5aa642c3d4066950dd1f01f4d"],
    ["hello world", "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed"],
    ["abcdx", "a96e144dfdc6380c8a4ae43bea1c81cb01215020"],
    ["long text, some spaces, blah blah", "fc0aa2379ecce86eac0dc50d921ab2cef7030d12"]
];

for(var idx=0; idx<demo.length; idx++)
{
    var itm = demo[idx];
    var k = itm[0];
    var v = itm[1];
    var ma = hex_sha1(k);
    var m = ma;
    var okstr = "FAIL";
    if(m == v)
    {
        okstr = "OK  ";
    }
    print(okstr, ": \"",k, "\" => ", m, "\n");

}
