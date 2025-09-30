; factorial.ll

; To run this code:
; Execute it with the LLVM interpreter: lli fact.ll
; Check the exit code: echo $?
;    The exit code will be the result of factorial(5), which is 520.

define i32 @factorial(i32 %n) {
entry:
  %cmp = icmp sle i32 %n, 1
  br i1 %cmp, label %return, label %loop

loop:
  %i = phi i32 [ %n, %entry ], [ %dec, %loop ]
  %acc = phi i32 [ 1, %entry ], [ %mul, %loop ]

  %dec = sub nsw i32 %i, 1
  %mul = mul nsw i32 %acc, %i
  %cmp.next = icmp sgt i32 %dec, 1
  br i1 %cmp.next, label %loop, label %return

return:
  %retval = phi i32 [ 1, %entry ], [ %mul, %loop ]
  ret i32 %retval
}

define i32 @main() {
entry:
  %result = call i32 @factorial(i32 5)
  ret i32 %result
}
