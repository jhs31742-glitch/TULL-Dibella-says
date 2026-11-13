Scriptname TDS_ImpulseEffect extends ActiveMagicEffect
Int Property Mode = 0 Auto
Int _ticket = -1
TDS_ImpulseController _controller
Event OnEffectStart(Actor target, Actor caster)
    Actor player = Game.GetPlayer()
    If !player || caster != player || !target
        Return
    EndIf
    Quest owner = Game.GetFormFromFile(0xF66, "TULL Dibella says.esp") as Quest
    If !owner
        Return
    EndIf
    _controller = owner.GetAlias(0) as TDS_ImpulseController
    If !_controller
        Message notice = Game.GetFormFromFile(0xF6B, "TULL Dibella says.esp") as Message
        If notice
            notice.Show()
        EndIf
        Return
    EndIf
    If Mode == 1
        If target == player
            _controller.StartSolo(player)
        EndIf
    Else
        _ticket = _controller.RegisterActor(target)
    EndIf
EndEvent
Event OnEffectFinish(Actor target, Actor caster)
    If Mode == 0 && _controller && _ticket >= 0
        _controller.FinishSelection(_ticket)
    EndIf
EndEvent
