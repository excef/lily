var a = :asdf
var b: list[symbol] = [:1, :2, :3]
var c = "".to_sym()
var d = string::to_sym
var e: symbol = d("10")

if :1 != "1".to_sym():
    print("Failed.\n")

define symbol_vararg(x: list[symbol]...) {

}

symbol_vararg(:1, :2, :3)

define return_input[A](x: A => A) {
    return x
}

var f: symbol = return_input("1".to_sym())

a = "( ͡° ͜ʖ ͡°)".to_sym()

if :"ä ę ĩ ō ų" != :"ä ę ĩ ō ų":
    print("Failed.\n")

a = :"""

"""

var g: hash[symbol, integer] = [:1 => 1, :2 => 2, :3 => 3]
