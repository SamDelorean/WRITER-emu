//@category Writer
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;

public class WriterKeyboardPhase3 extends GhidraScript {
  private int u(int a) throws Exception { return currentProgram.getMemory().getByte(toAddr(a)) & 0xff; }
  private void lab(long a,String s) throws Exception { createLabel(toAddr(a),s,true); }
  private void c(long a,String s) throws Exception { currentProgram.getListing().setComment(toAddr(a),CodeUnit.PRE_COMMENT,s); }
  public void run() throws Exception {
    String out=getScriptArgs()[0]; new File(out).mkdirs();
    lab(0x0812,"DISPLAY_WRITE_7BIT_CHARACTER_OR_COMMAND");
    lab(0x0837,"DISPLAY_WRITE_COMMAND_BYTE");
    lab(0x0874,"DISPLAY_DEFINE_ONE_8BYTE_CUSTOM_GLYPH");
    lab(0x0890,"DISPLAY_DEFINE_CUSTOM_GLYPH_ON_BOTH_PORTS");
    lab(0x08C5,"DISPLAY_INITIALIZE_FOUR_BUILTIN_GLYPHS");
    c(0x0812,"Display output primitive. If L bit 7 is clear, sends L|0x80 as character data to port 0xC0, then performs the controller handshake. If L bit 7 is set, branches to 0x0837 and uses the value as a controller command. High-bit bytes are therefore not passed as ordinary text characters through this interface.");
    c(0x0874,"Loads one custom glyph: first byte selects glyph slot (command 0x40 + 8*(slot-8)); next eight bytes are bitmap rows. Called by 0x0890 for both display-controller ports.");
    c(0x0890,"Writes one 8-byte custom glyph definition to both display-controller ports (0xC0 and 0xC2).");
    c(0x08C5,"Initializes four fixed custom glyphs from ROM bitmap records at 0x08A1, 0x08AA, 0x08B3, and 0x08BC. These are display glyph definitions; no keyboard mapping to accented letters was identified in this path.");
    for(long a:new long[]{0x0812,0x0837,0x0874,0x0890,0x08C5}){try{disassemble(toAddr(a));}catch(Exception e){}}
    PrintWriter p=new PrintWriter(new FileWriter(out+"/keyboard_phase3_display_disassembly.txt"));
    for(int[] r:new int[][]{{0x0812,0x0854},{0x0874,0x08A1},{0x08A1,0x08C5},{0x08C5,0x08E0}}){
      p.printf("\n### %04X-%04X%n",r[0],r[1]); int a=r[0];
      while(a<r[1]){Instruction in=currentProgram.getListing().getInstructionAt(toAddr(a));
        if(in==null){p.printf("%04X: %02X [data/unknown]%n",a,u(a));a++;}
        else {p.printf("%s: %02X %s%n",in.getAddress(),u(a),in.toString());a+=(int)in.getLength();}}
    }
    p.close();
    PrintWriter t=new PrintWriter(new FileWriter(out+"/keyboard_phase3_builtin_glyphs.tsv"));
    t.println("record_address\tslot\tbitmap_row_0\tbitmap_row_1\tbitmap_row_2\tbitmap_row_3\tbitmap_row_4\tbitmap_row_5\tbitmap_row_6\tbitmap_row_7");
    for(int a:new int[]{0x08A1,0x08AA,0x08B3,0x08BC}){
      t.printf("%04X\t%02X",a,u(a));
      for(int i=1;i<=8;i++)t.printf("\t%02X",u(a+i));
      t.println();
    }
    t.close();
    println("WRITER_KEYBOARD_PHASE3_OK output="+out);
  }
}