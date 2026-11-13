Scriptname TDS_ImpulseController extends ReferenceAlias
Actor[] _slots
Int _count = 0
Int _generation = 1
Bool _busy = False

Event OnInit()
    ResetSelection()
    GrantSpells()
EndEvent
Event OnPlayerLoadGame()
    ResetSelection()
    GrantSpells()
EndEvent
Function GrantSpells()
    Actor player = GetActorReference()
    If !player
        Return
    EndIf
    Spell selfSpell = Game.GetFormFromFile(0xF60, "TULL Dibella says.esp") as Spell
    Spell targetSpell = Game.GetFormFromFile(0xF61, "TULL Dibella says.esp") as Spell
    Spell soloPower = Game.GetFormFromFile(0xF62, "TULL Dibella says.esp") as Spell
    If selfSpell && !player.HasSpell(selfSpell)
        player.AddSpell(selfSpell, False)
    EndIf
    If targetSpell && !player.HasSpell(targetSpell)
        player.AddSpell(targetSpell, False)
    EndIf
    If soloPower && !player.HasSpell(soloPower)
        player.AddSpell(soloPower, False)
    EndIf
EndFunction
Function ResetSelection()
    _generation += 1
    _slots = New Actor[5]
    _count = 0
    _busy = False
EndFunction
SexLabFramework Function GetAPI()
    If !Game.GetFormFromFile(0xD62, "SexLab.esm")
        Return None
    EndIf
    Return SexLabUtil.GetAPI()
EndFunction
Bool Function ValidActor(SexLabFramework api, Actor who)
    If !api || !who
        Return False
    EndIf
    If who.IsDead() || who.IsDisabled() || who.IsChild() || api.IsActorActive(who)
        Return False
    EndIf
    Return api.IsValidActor(who)
EndFunction
Int Function RegisterActor(Actor who)
    If _busy
        Return -1
    EndIf
    _busy = True
    SexLabFramework api = GetAPI()
    If !ValidActor(api, who)
        _busy = False
        Notify(0xF67)
        Return -1
    EndIf
    If !_slots
        _slots = New Actor[5]
        _count = 0
    EndIf
    If _count >= 5 || _slots.Find(who) >= 0
        _busy = False
        Return -1
    EndIf
    _slots[_count] = who
    _count += 1
    Int ticket = _generation
    _busy = False
    If _count == 5
        FinishSelection(ticket)
    EndIf
    Return ticket
EndFunction
Actor[] Function AllocateActors(Int count)
    If count == 1
        Return New Actor[1]
    ElseIf count == 2
        Return New Actor[2]
    ElseIf count == 3
        Return New Actor[3]
    ElseIf count == 4
        Return New Actor[4]
    ElseIf count == 5
        Return New Actor[5]
    EndIf
    Return None
EndFunction
Function FinishSelection(Int ticket)
    If _busy || ticket != _generation || _count < 1 || _count > 5
        Return
    EndIf
    _busy = True
    Actor[] actors = AllocateActors(_count)
    Int i = 0
    While i < _count
        actors[i] = _slots[i]
        i += 1
    EndWhile
    ResetSelection()
    StartScene(actors, False)
EndFunction
Function StartSolo(Actor who)
    If _busy || who != GetActorReference()
        Return
    EndIf
    ResetSelection()
    Actor[] actors = New Actor[1]
    actors[0] = who
    StartScene(actors, True)
EndFunction
Function StartScene(Actor[] actors, Bool solo)
    If _busy || !actors
        Return
    EndIf
    _busy = True
    SexLabFramework api = GetAPI()
    Int i = 0
    While i < actors.Length
        If !ValidActor(api, actors[i])
            _busy = False
            Notify(0xF68)
            Return
        EndIf
        i += 1
    EndWhile
    sslBaseAnimation[] anims
    If solo
        sslBaseAnimation[] compatible = api.PickAnimationsByActors(actors, 128, False)
        If compatible
            Int candidateIndex = 0
            While candidateIndex < compatible.Length
                sslBaseAnimation candidate = compatible[candidateIndex]
                If candidate && candidate.HasTag("Masturbation")
                    anims = sslUtility.PushAnimation(candidate, anims)
                EndIf
                candidateIndex += 1
            EndWhile
        EndIf
        If !anims || anims.Length < 1
            _busy = False
            Notify(0xF69)
            Return
        EndIf
    EndIf
    Actor[] sorted = api.SortActors(actors, True)
    If !sorted || sorted.Length != actors.Length
        _busy = False
        Return
    EndIf
    Int threadID = api.StartSex(sorted, anims, None, None, True, "TDSImpulse")
    Debug.Trace("TDS_SCENE_REQUEST solo=" + solo + " actors=" + actors.Length + " thread=" + threadID)
    SendModEvent("TDSSceneRequest", "solo=" + solo + " actors=" + actors.Length, threadID)
    If threadID < 0
        Notify(0xF6A)
    EndIf
    _busy = False
EndFunction

Function Notify(Int localID)
    Message notice = Game.GetFormFromFile(localID, "TULL Dibella says.esp") as Message
    If notice
        notice.Show()
    EndIf
EndFunction
