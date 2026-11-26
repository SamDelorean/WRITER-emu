//@category Writer
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;

public class WriterKeyboardPhase2 extends GhidraScript {
  private int u(int a) throws Exception { return currentProgram.getMemory().getByte(toAddr(a)) & 0xff; }
  private void lab(long a,String s) throws Exception { createLabel(toAddr(a),s,true); }
  private void c(long a,String s) throws Exception { currentProgram.getListing().setComment(toAddr(a),CodeUnit.PRE_COMMENT,s); }
  private String bs(int a,int n) throws Exception { StringBuilder b=new StringBuilder(); for(int i=0;i<n;i++){if(i>0)b.append(' ');b.append(String.format("%02X",u(a+i)));} return b.toString(); }
  private void dump(PrintWriter p,int lo,int hi) throws Exception {
    p.printf("\n### %04X-%04X%n",lo,hi); int a=lo;
    while(a<hi){ Instruction in=currentProgram.getListing().getInstructionAt(toAddr(a));
      if(in==null){p.printf("%04X: %02X [not disassembled/data/unknown]%n",a,u(a));a++;}
      else {p.printf("%s: %s %s%n",in.getAddress(),bs(a,(int)in.getLength()),in.toString());a+=(int)in.getLength();}
    }
  }
  public void run() throws Exception {
    String out=getScriptArgs()[0]; new File(out).mkdirs();
    String[][] labels={
      {"0D24","ALPHA_FILTER_SET_B_27_BYTES"},{"0D3F","ALPHA_FILTER_SET_A_22_BYTES"},
      {"0D55","KEY_EVENT_DISPATCH"},{"0D68","KEY_HANDLER_NO_OUTPUT"},
      {"0D8C","KEY_HANDLER_TOGGLE_STATE"},{"0D94","KEY_HANDLER_RETURN_CODE_NO_MODIFIERS"},
      {"0D9A","KEY_HANDLER_RETURN_CODE_WITH_MODIFIERS"},{"0DA4","KEY_HANDLER_SYMBOL_TRANSLATION"},
      {"0DBC","KEY_HANDLER_ALPHA_TRANSLATION"},{"0DFD","KEY_RESULT_SAVE_CURSOR_CONTEXT"},
      {"0E08","KEYBOARD_READ_EVENT_BLOCKING"},{"0E4C","KEYBOARD_READ_EVENT_AND_SET_PENDING"},
      {"0E58","KEYBOARD_DRAIN_EVENTS"},{"0E62","KEYBOARD_SET_CURSOR_CONTEXT"},
      {"0E6E","KEYBOARD_SET_REPEAT_DELAY"},{"0E77","KEYBOARD_SET_MODE_BITS"},
      {"0E9B","KEYBOARD_RESET_STATE"},{"0EB8","KEYBOARD_RESET_MODE_DEFAULTS"},
      {"25DE","EDITOR_KEYBOARD_INPUT_LOOP"},{"25FD","EDITOR_DEFAULT_ASCII_INSERT_HANDLER"},{"2682","EDITOR_KEY_RESULT_SWITCH"},
      {"25E8","EDITOR_SPECIAL_KEY_CALLBACK_TABLE"},{"24A7","MASTER_RESET_SPECIAL_KEY_CALLBACK_TABLE"},
      {"257A","MASTER_RESET_PASSWORD_BUFFER_EDITOR"},{"2636","EDITOR_BACKSPACE_HANDLER"},
      {"266E","EDITOR_CLEAR_CURRENT_CHARACTER_HANDLER"},{"2673","EDITOR_SPECIAL_RESULT_HANDLER"},
      {"2677","EDITOR_ENTER_OR_ACCEPT_HANDLER"},{"01B0","API_KEYBOARD_SET_MODE"},
      {"01BC","API_KEYBOARD_READ_MODIFIER_STATE"},{"01C0","API_KEYBOARD_FIFO_DEQUEUE"},
      {"01CC","API_KEYBOARD_DISPATCH_EVENT"},{"01D0","API_KEYBOARD_READ_EVENT"}
    };
    for(String[] x:labels) lab(Long.parseLong(x[0],16),x[1]);
    c(0x0D24,"27-byte membership set consulted by alphabetic handler 0x0DBC when keyboard mode flag D085 is enabled and exactly one of D088 bits 0/1 is active. This is a character-selection filter, not an accent/conversion table.");
    c(0x0D3F,"22-byte membership set consulted by alphabetic handler 0x0DBC under the other D088 bit-0/bit-1 case. This is a character-selection filter, not an accent/conversion table.");
    c(0x0DA4,"Symbol handler: starts with entry byte +2; if modifier bits 2..4 are clear and either modifier bit 0 or 1 is set, selects entry byte +3. Otherwise keeps base byte.");
    c(0x0DBC,"Alphabetic handler: returns base when D088=0; otherwise selects entry byte +3. D085 gates a further two-modifier filter: if exactly one of D088 bits 0/1 is active, selected output must occur in the corresponding membership set at 0x0D3F or 0x0D24, else no output.");
    c(0x0D94,"Special/control handler: returns the entry's byte +2 in L, H=0, through the common result path. This is an opaque key code, not necessarily a character.");
    c(0x0D9A,"Special handler: returns entry byte +2 in L and D088&0x1F in H. The byte is an action/control code, not necessarily a character.");
    c(0x0E77,"Keyboard mode setter: input HL bits 0,1,2 populate D085, D086 (0x00/0xFF), and D087 (0xE0/0xC0) respectively. Reset routine 0x0E9B calls this with HL=0.");
    c(0x0E08,"Blocking keyboard event reader: calls dispatcher 0x0D55 and returns its 16-bit result in HL. No printable-character insertion occurs here; the caller consumes the result.");
    c(0x25FD,"Default editor key handler after the inline special-code table: accepts low-byte values 0x20 through 0x7F only, rejects results when D has any bits in mask 0x1C, checks buffer bounds, then inserts E into the editor buffer at D1E9.");
    c(0x2682,"Inline-table switch dispatcher. The table immediately following CALL 0x2682 consists of 16-bit key/result codes followed by 16-bit handler addresses, terminated by a zero code.");
    c(0x25E8,"Editor input callback table: 0x00F8->0x2673, 0x00A7->0x2673, 0x001B->0x266E, 0x000D->0x2677, 0x0008->0x2636, then zero terminator. Context-specific behavior only; do not assign legends to opaque codes.");
    c(0x24A7,"Master Reset input callback table begins here; entries include 0x00F8->0x2557, 0x00A7->0x2563, 0x001B->0x2557, then terminator.");
    c(0xD085,"Keyboard mode flag loaded from bit 0 by 0x0E77; gates alphabetic alternate-character filtering in 0x0DBC. Semantics/label from UI not yet established.");
    c(0xD086,"Keyboard scan/index option loaded as sign-extended bit 1 by 0x0E77 and ORed into scan result by 0x0BD3.");
    c(0xD087,"Keyboard modifier-state preservation mask loaded from bit 2 by 0x0E77 (0xE0 or 0xC0); applied by dispatcher 0x0D55.");
    c(0xD088,"Keyboard modifier/state byte. Event modifier bits are merged here by dispatcher; handlers 0x0DA4/0x0DBC inspect it.");
    c(0xD0AF,"Pending 16-bit keyboard result. Dispatcher returns and clears it before dequeuing another FIFO event.");
    long[] code={0x0D55,0x0D68,0x0D8C,0x0D94,0x0D9A,0x0DA4,0x0DBC,0x0DFD,0x0E08,0x0E4C,0x0E58,0x0E62,0x0E6E,0x0E77,0x0E9B,0x0EB8,0x25DE,0x25FD,0x2636,0x266E,0x2673,0x2677,0x2682,0x257A};
    for(long a:code){try{disassemble(toAddr(a));}catch(Exception e){}}
    PrintWriter p=new PrintWriter(new FileWriter(out+"/keyboard_phase2_handlers.txt"));
    for(int[] r:new int[][]{{0x0D20,0x0EBD},{0x24A1,0x24C3},{0x25DE,0x267D},{0x2682,0x26A2},{0x01B0,0x01E0}}) dump(p,r[0],r[1]);
    p.close();
    PrintWriter t=new PrintWriter(new FileWriter(out+"/keyboard_phase2_tables.tsv"));
    t.println("table\tindex\trom_entry\thandler\tdata0\tdata1\tclassification");
    for(int i=0;i<128;i++){int a=0x0964+4*i;int h=u(a)|(u(a+1)<<8);String cl=h==0x0D94?"special_code_no_modifiers":h==0x0D9A?"special_code_with_modifiers":h==0x0D8C?"state_toggle":h==0x0DA4?"symbol":h==0x0DBC?"alphabetic":"no_output_or_other";
      t.printf("keymap\t%02X\t%04X\t%04X\t%02X\t%02X\t%s%n",i,a,h,u(a+2),u(a+3),cl);
    }
    t.close();
    println("WRITER_KEYBOARD_PHASE2_OK output="+out);
  }
}