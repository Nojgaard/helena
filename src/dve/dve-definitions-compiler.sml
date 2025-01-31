(*
 *  File:
 *     dve-definitions-compiler.sml
 *)


structure
DveDefinitionsCompiler:
sig
    
    val gen:
        System.system * TextIO.outstream * TextIO.outstream
	-> unit
                    
    val compileProcessState:
        Process.process
        -> string
                                    
end = struct

open DveCompilerUtils
         
fun compileProcessState p = let
    val num     = ref 0
    val pName   = Process.getName p
    val pInit   = Process.getInit p
    val pStates = Process.getStates p
in
    num := 0
  ; listFormat { init  = "",
		 sep   = "\n",
		 final = "\n",
		 fmt   = (fn s => 
			     String.concat [
				 "#define ",
				 getLocalStateName (pName, State.getName s),
				 " ", Int.toString (!num) ]
			     before num := !num + 1) }
	       pStates
end

fun compileStateType s = let
    fun compileProcessStateTypes () =
      String.concat (List.map compileProcessState (System.getProcs s))
                    
    fun compileArrayTypes () = let
	fun compileArrayType (bt, n) =
	  SOME ("typedef " ^ (typeName (Typ.BASIC_TYPE bt)) ^ " " ^
		(typeName (Typ.ARRAY_TYPE (bt, n)))  ^ "[" ^
		(Int.toString n) ^ "];\n")
	val lv = List.concat (List.map Process.getVars (System.getProcs s))
	val gv = System.getVars s
	val t = List.map Var.getTyp (lv @ gv)
	val t = List.mapPartial
		    (fn (Typ.BASIC_TYPE _) => NONE
		    | (Typ.ARRAY_TYPE (bt, n)) => SOME (bt, n)) t
	val t = ListMergeSort.uniqueSort
		    (fn ((Typ.INT, n), (Typ.INT, m)) => Int.compare (n, m)
		    | ((Typ.BYTE, n), (Typ.BYTE, m)) => Int.compare (n, m)
		    | ((Typ.INT, n), _) => GREATER
		    | _ => LESS) t
    in
	String.concat (List.mapPartial compileArrayType t) 
    end

    fun compileComp comp =
      SOME (String.concat [ getCompTypeName comp, " ",
			    getCompName comp, ";" ])
    val processStateTypeDefs = compileProcessStateTypes ()
    val comps = buildStateComps s
    val (consts, comps) = List.partition isCompConst comps
    val arrayTypeDefs = compileArrayTypes ()

    fun compileConst comp = let
	val v = valOf (getCompVar comp)
	val t = Var.getTyp v
    in
	SOME (typeName t ^ " " ^ (getCompName comp) ^ ";")
    end

    fun compilePrintComp comp = let
	val compName = getCompName comp
	fun printVar (field, txt, Typ.BASIC_TYPE _) =
	  "fprintf(out, \"   " ^ txt ^ " = %d\\n\", s->" ^ field ^ ");"
	  | printVar (field, txt, Typ.ARRAY_TYPE (bt, n)) =
	    concatLines
	        (List.map (fn e => (if e = 0 then "" else "   ") ^
                                   (printVar
                                        (field ^ "[" ^ (Int.toString e) ^ "]",
					 txt ^ "[" ^ (Int.toString e) ^ "]",
					 Typ.BASIC_TYPE bt)))
		          (List.tabulate (n, fn x => x)))
        val result =
            case comp
	     of GLOBAL_VAR { name, typ, ... } =>
		printVar (compName, name, typ)
	      | LOCAL_VAR (s, { name, typ, ... }) =>
		printVar (compName, "   " ^ name, typ)
	      | PROCESS_STATE st => let
                  val p = System.getProc (s, st)
                  val pName   = Process.getName p
                  val pStates = Process.getStates p
                  val cases =
                      List.map
                          (fn s =>
                              "      case " ^
			      getLocalStateName (pName, State.getName s) ^
                              ": fprintf(out, \"" ^ (State.getName s) ^
                              "\\n\"); break;") pStates
              in                                        
                  concatLines [
		      "fprintf(out, \"   " ^ st ^ " @ \");",
                      "   switch(s->" ^ compName ^ ") {",
                      concatLines cases,
                      "      default: assert(0);",
                      "   }" ]
              end
    in SOME result end
    val printComps =
	ListMergeSort.sort
	    (fn (GLOBAL_VAR {name = n1, ...}, GLOBAL_VAR {name = n2, ...}) =>
		String.> (n1, n2)
	    | (GLOBAL_VAR _, _) => false
	    | (_, GLOBAL_VAR _) => true
	    | (LOCAL_VAR (p1, {name = n1, ...}),
	       LOCAL_VAR (p2, {name = n2, ...})) =>
	      String.> (p1, p2) orelse (p1 = p2 andalso String.> (n1, n2))
	    | (PROCESS_STATE p1, PROCESS_STATE p2) => String.> (p1, p2)
	    | (LOCAL_VAR (p1, _), PROCESS_STATE p2) => String.> (p1, p2)
	    | (PROCESS_STATE p1, LOCAL_VAR (p2, _)) => String.> (p1, p2)
	    )
	    comps
    val stateVectorSize = sizeofComps comps
in
    ([ processStateTypeDefs,
       "/*  state type  */",
       arrayTypeDefs,
       "typedef struct {",
       Utils.fmt {init  = "   ",
		  sep   = "\n   ",
		  final = "\n   heap_t heap;\n} struct_mstate_t;",
		  fmt   = compileComp} comps,
       "#define MODEL_STATE_SIZE " ^ (Int.toString stateVectorSize),
       "typedef struct_mstate_t * mstate_t;",
       "void mstate_free(mstate_t s);",
       "mstate_t mstate_copy(mstate_t s, heap_t heap);",
       "bool_t mstate_equal(mstate_t s1, mstate_t s2);",
       "void mstate_print(mstate_t s, FILE * out);",
       "",
       Utils.fmt {init  = if consts <> [] then "/*  constants  */\n" else "",
		  sep   = "\n",
		  final = "",
		  fmt   = compileConst} consts
     ],
     [ "void mstate_free(mstate_t s) { mem_free (s->heap, s);}",
       "",
       "mstate_t mstate_copy",
       "(mstate_t s,",
       " heap_t heap) {",
       "   mstate_t result = mem_alloc(heap, sizeof (struct_mstate_t));",
       "   *result = *s;",
       "   result->heap = heap;",
       "   return result;",
       "}",
       "",
       "bool_t mstate_equal",
       "(mstate_t s1,",
       " mstate_t s2) {",
       "   return (0 == memcmp(s1, s2, MODEL_STATE_SIZE));",
       "}",
       "",
       "void mstate_print",
       "(mstate_t s,",
       " FILE * out) {",
       "   fprintf (out, \"{\\n\");",
       Utils.fmt {init  = "   ",
		  sep   = "\n   ",
		  final = "\n",
		  fmt   = compilePrintComp} printComps,
       "   fprintf (out, \"}\\n\");",
       "}"
    ])
end

fun compileEventType (s: System.system) = let
    fun isProcEvent (name, LOCAL (_, p, _)) = p = name
      | isProcEvent (name, SYNC (_, _, p, _, q, _)) = p = name orelse q = name
    fun getProcEvents (events, name) =
      List.filter (fn e => isProcEvent (name, e)) events
    val systemEvents = buildEvents s
    fun desc e =
      "      case " ^ (getEventName e) ^ ": fprintf(out, \"" ^
      (case e
        of LOCAL (_, p, t) =>
           p ^ ": line " ^ (Int.toString (Trans.getPos t))
         | SYNC (_, _, p, tp, q, tq) =>
           "(" ^ p ^ ": line " ^ (Int.toString (Trans.getPos tp)) ^ "," ^
           " " ^ q ^ ": line " ^ (Int.toString (Trans.getPos tq)) ^ ")") ^
      "\\n\"); break;"
    val printCases = concatLines (List.map desc systemEvents)
    val num = ref 0
in
    ([ "#define NO_EVENTS " ^ (Int.toString (List.length (systemEvents))),
       listFormat {init  = "",
		   sep   = "\n",
		   final = "\n",
		   fmt   = fn e => ("#define " ^ (getEventName e) ^ " " ^
				    (Int.toString (!num)
				     before (num := !num + 1))) }
		  systemEvents,
       "uint32_t mevent_char_size(mevent_t e);",
       "void mevent_free(mevent_t e);",
       "mevent_t mevent_copy(mevent_t e, heap_t h);",
       "void mevent_print(mevent_t e, FILE * out);",
       "mevent_id_t mevent_id(mevent_t e);",
       "order_t mevent_cmp(mevent_t e, mevent_t f);",
       "bool_t mevent_is_safe(mevent_t e);",
       "unsigned int mevent_safe_set(mevent_t e);",
       "bool_t mevent_is_visible(mevent_t e);" ],
     [
       "uint32_t mevent_char_size(mevent_t e) { return sizeof(mevent_t); }",
       "",
       "void mevent_free(mevent_t e) {}",
       "",
       "mevent_t mevent_copy(mevent_t e, heap_t h) { return e; }",
       "",
       "mevent_id_t mevent_id(mevent_t e) { return e; }",
       "",
       "bool_t mevent_is_safe(mevent_t e) { return FALSE; }",
       "",
       "unsigned int mevent_safe_set(mevent_t e) { return 0; }",
       "",
       "bool_t mevent_is_visible(mevent_t e) { return TRUE; }",
       "",
       "void mevent_print(mevent_t e, FILE * out) {",
       "   switch(e) {",
       printCases,
       "      default: assert(0);",
       "   }",
       "}",
       "",
       "order_t mevent_cmp(mevent_t e, mevent_t f) {",
       "   if(e < f) return LESS;",
       "   if(e > f) return GREATER;",
       "   return EQUAL;",
       "}"
    ])
end

fun gen (s, hFile, cFile) = let
    val (eventDefH, eventDefC) = compileEventType s
    val (eventDefH, eventDefC) = (concatLines eventDefH, concatLines eventDefC)
    val (stateDefH, stateDefC) = compileStateType s
    val (stateDefH, stateDefC) = (concatLines stateDefH, concatLines stateDefC)
    val comps = buildStateComps s
    val map = buildMapping comps
    val (consts, comps) = List.partition isCompConst comps
    fun compileConstInit comp = let
        val v = valOf (getCompVar comp)
        val name = getCompName comp
        val t = Var.getTyp v
        val i = valOf(Var.getInit v)
        val assigns = (0, "")
    in
        case (t, i) of
            (Typ.ARRAY_TYPE (bt, n), Expr.ARRAY_INIT(_, l)) => let
             val assigns =
                 List.foldr
                     (fn (e, (idx, assigns)) => let
                          val init = (compileExpr "" e
                                                  (NONE, map, comps, false))
                          val a = name ^ "[" ^ (Int.toString idx) ^ "] = " ^
                                  init ^ ";"
                      in (idx + 1, a :: assigns) end) (0, []) l
             val (_, assigns) = assigns
         in
             SOME (concatLines assigns)
         end
          | _ => (SOME ("   " ^ name ^ " = " ^
                        (compileExpr "" i (NONE, map, comps, false)) ^ ";"))
    end
in
    (*
     *  H file
     *)
    TextIO.output (
        hFile,
        concatLines [
            "/*  basic types  */",
            "typedef int32_t int_t;",
            "typedef uint8_t byte_t;",
            "typedef uint8_t proc_state_t;",
            "typedef uint16_t mevent_t;",
            "typedef mevent_t mevent_id_t;",
            "",
            "/*  event definition  */",
            eventDefH,
            "",
            "/*  state definition  */",
            stateDefH,
            "",
            "/*  model initialisation and termination  */",
            "void init_model();",
            "void finalise_model();" ])

  (*
   *  C file
   *)
  ; TextIO.output (
        cFile,
        concatLines [
            eventDefC,
            "",
            stateDefC,
            "",
            "void init_model () {",
            Utils.fmt {init  = "",
	               sep   = "\n",
	               final = "",
	               fmt   = compileConstInit} consts,
            "}",
            "",
            "void finalise_model () {",
            "}" ])
end

end
