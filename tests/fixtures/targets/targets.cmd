; x starts the test punch; the hold commands are the ones common1.cns needs to walk.
[Command]
name = "x"
command = x
time = 1

[Command]
name = "holdfwd"
command = /$F
time = 1

[Command]
name = "holdback"
command = /$B
time = 1

[Command]
name = "holdup"
command = /$U
time = 1

[Command]
name = "holddown"
command = /$D
time = 1

[Statedef -1]

[State -1, punch]
type = ChangeState
value = 1500
triggerall = command = "x"
trigger1 = ctrl && statetype = S
