; input.ll
define dso_local i32 @main(i32 %argc, ptr %argv) {
entry:
  %x = alloca i32
  store i32 10, i32* %x
  ret i32 0
}