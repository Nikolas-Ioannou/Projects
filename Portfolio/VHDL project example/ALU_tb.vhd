---------------------------------------------------------------------------------
-- Company: 
-- Engineer: 
-- 
-- Create Date: 11/11/2024 06:18:18 PM
-- Design Name: 
-- Module Name: ALU_tb - Behavioral
-- Project Name: 
-- Target Devices: 
-- Tool Versions: 
-- Description: 
-- 
-- Dependencies: 
-- 
-- Revision:
-- Revision 0.01 - File Created
-- Additional Comments:
-- 
----------------------------------------------------------------------------------


library IEEE;
use IEEE.STD_LOGIC_1164.ALL;

-- Uncomment the following library declaration if using
-- arithmetic functions with Signed or Unsigned values
--use IEEE.NUMERIC_STD.ALL;

-- Uncomment the following library declaration if instantiating
-- any Xilinx leaf cells in this code.
--library UNISIM;
--use UNISIM.VComponents.all;

entity ALU_tb is
-- Port ( );
end ALU_tb;

architecture Beh_tb of ALU_tb is
component ALU is port(  a : in STD_LOGIC_VECTOR (3 downto 0);
           Control : in STD_LOGIC_VECTOR (2 downto 0);
           Result : out STD_LOGIC_VECTOR (3 downto 0));
end component;
signal a_tb: STD_LOGIC_VECTOR (3 downto 0);
signal Control_tb: STD_LOGIC_VECTOR (2 downto 0);
signal Result_tb: STD_LOGIC_VECTOR (3 downto 0);
begin
utt: ALU Port map ( a=> a_tb,Control=> Control_tb,Result => Result_tb);
apply_test_cases:process is
begin 
a_tb <= "0000"; Control_tb <= "000"; wait for 20 ns; 
        a_tb <= "0000"; Control_tb <= "001"; wait for 20 ns;
        a_tb <= "0000"; Control_tb <= "010"; wait for 20 ns;
        a_tb <= "0000"; Control_tb <= "011"; wait for 20 ns;
        a_tb <= "0000"; Control_tb <= "100"; wait for 20 ns;
        a_tb <= "0000"; Control_tb <= "101"; wait for 20 ns;
        a_tb <= "0000"; Control_tb <= "110"; wait for 20 ns;
        a_tb <= "0000"; Control_tb <= "111"; wait for 20 ns;
        a_tb <= "0001"; Control_tb <= "000"; wait for 20 ns;
        a_tb <= "0001"; Control_tb <= "001"; wait for 20 ns;
        a_tb <= "0001"; Control_tb <= "010"; wait for 20 ns;
        a_tb <= "0001"; Control_tb <= "011"; wait for 20 ns;
        a_tb <= "0001"; Control_tb <= "100"; wait for 20 ns;
        a_tb <= "0001"; Control_tb <= "101"; wait for 20 ns;
        a_tb <= "0001"; Control_tb <= "110"; wait for 20 ns;
        a_tb <= "0001"; Control_tb <= "111"; wait for 20 ns;
        a_tb <= "0010"; Control_tb <= "000"; wait for 20 ns;
        a_tb <= "0010"; Control_tb <= "001"; wait for 20 ns;
        a_tb <= "0010"; Control_tb <= "010"; wait for 20 ns;
        a_tb <= "0010"; Control_tb <= "011"; wait for 20 ns;
        a_tb <= "0010"; Control_tb <= "100"; wait for 20 ns;
        a_tb <= "0010"; Control_tb <= "101"; wait for 20 ns;
        a_tb <= "0010"; Control_tb <= "110"; wait for 20 ns;
        a_tb <= "0010"; Control_tb <= "111"; wait for 20 ns;
        a_tb <= "0011"; Control_tb <= "000"; wait for 20 ns;
        a_tb <= "0011"; Control_tb <= "001"; wait for 20 ns;
        a_tb <= "0011"; Control_tb <= "010"; wait for 20 ns;
        a_tb <= "0011"; Control_tb <= "011"; wait for 20 ns;
        a_tb <= "0100"; Control_tb <= "000"; wait for 20 ns;
        a_tb <= "0100"; Control_tb <= "001"; wait for 20 ns;
        a_tb <= "0100"; Control_tb <= "010"; wait for 20 ns;
        a_tb <= "0100"; Control_tb <= "011"; wait for 20 ns;
        a_tb <= "0101"; Control_tb <= "000"; wait for 20 ns;
        a_tb <= "0101"; Control_tb <= "001"; wait for 20 ns;
        a_tb <= "0101"; Control_tb <= "010"; wait for 20 ns;
        a_tb <= "0101"; Control_tb <= "011"; wait for 20 ns;
        a_tb <= "0110"; Control_tb <= "000"; wait for 20 ns;
        a_tb <= "0110"; Control_tb <= "001"; wait for 20 ns;
        a_tb <= "0110"; Control_tb <= "010"; wait for 20 ns;
        a_tb <= "0110"; Control_tb <= "011"; wait for 20 ns;
        a_tb <= "0111"; Control_tb <= "000"; wait for 20 ns;
        a_tb <= "0111"; Control_tb <= "001"; wait for 20 ns;
        a_tb <= "0111"; Control_tb <= "010"; wait for 20 ns;
        a_tb <= "0111"; Control_tb <= "011"; wait for 20 ns;
        a_tb <= "1000"; Control_tb <= "000"; wait for 20 ns;
        a_tb <= "1000"; Control_tb <= "001"; wait for 20 ns;
        a_tb <= "1000"; Control_tb <= "010"; wait for 20 ns;
        a_tb <= "1000"; Control_tb <= "011"; wait for 20 ns;
        a_tb <= "1000"; Control_tb <= "100"; wait for 20 ns;
        a_tb <= "1000"; Control_tb <= "101"; wait for 20 ns;
        a_tb <= "1000"; Control_tb <= "110"; wait for 20 ns;
        a_tb <= "1000"; Control_tb <= "111"; wait for 20 ns;
        a_tb <= "1001"; Control_tb <= "000"; wait for 20 ns;
        a_tb <= "1001"; Control_tb <= "001"; wait for 20 ns;
        a_tb <= "1001"; Control_tb <= "010"; wait for 20 ns;
        a_tb <= "1001"; Control_tb <= "011"; wait for 20 ns;
        a_tb <= "1001"; Control_tb <= "100"; wait for 20 ns;
        a_tb <= "1001"; Control_tb <= "101"; wait for 20 ns;
        a_tb <= "1001"; Control_tb <= "110"; wait for 20 ns;
        a_tb <= "1001"; Control_tb <= "111"; wait for 20 ns;
        a_tb <= "1010"; Control_tb <= "000"; wait for 20 ns;
        a_tb <= "1010"; Control_tb <= "001"; wait for 20 ns;
        a_tb <= "1010"; Control_tb <= "010"; wait for 20 ns;
        a_tb <= "1010"; Control_tb <= "011"; wait for 20 ns;
        a_tb <= "1010"; Control_tb <= "100"; wait for 20 ns;
        a_tb <= "1010"; Control_tb <= "101"; wait for 20 ns;
        a_tb <= "1010"; Control_tb <= "110"; wait for 20 ns;
        a_tb <= "1010"; Control_tb <= "111"; wait for 20 ns;
        a_tb <= "1011"; Control_tb <= "000"; wait for 20 ns;
        a_tb <= "1011"; Control_tb <= "001"; wait for 20 ns;
        a_tb <= "1011"; Control_tb <= "010"; wait for 20 ns;
        a_tb <= "1011"; Control_tb <= "011"; wait for 20 ns;
        a_tb <= "1011"; Control_tb <= "100"; wait for 20 ns;
        a_tb <= "1011"; Control_tb <= "101"; wait for 20 ns;
        a_tb <= "1011"; Control_tb <= "110"; wait for 20 ns;
        a_tb <= "1011"; Control_tb <= "111"; wait for 20 ns;
        a_tb <= "1100"; Control_tb <= "000"; wait for 20 ns;
        a_tb <= "1100"; Control_tb <= "001"; wait for 20 ns;
        a_tb <= "1100"; Control_tb <= "010"; wait for 20 ns;
        a_tb <= "1100"; Control_tb <= "011"; wait for 20 ns;
        a_tb <= "1100"; Control_tb <= "100"; wait for 20 ns;
        a_tb <= "1100"; Control_tb <= "101"; wait for 20 ns;
        a_tb <= "1100"; Control_tb <= "110"; wait for 20 ns;
        a_tb <= "1100"; Control_tb <= "111"; wait for 20 ns;
        a_tb <= "1101"; Control_tb <= "000"; wait for 20 ns;
        a_tb <= "1101"; Control_tb <= "001"; wait for 20 ns;
        a_tb <= "1101"; Control_tb <= "010"; wait for 20 ns;
        a_tb <= "1101"; Control_tb <= "011"; wait for 20 ns;
        a_tb <= "1101"; Control_tb <= "100"; wait for 20 ns;
        a_tb <= "1101"; Control_tb <= "101"; wait for 20 ns;
        a_tb <= "1101"; Control_tb <= "110"; wait for 20 ns;
        a_tb <= "1101"; Control_tb <= "111"; wait for 20 ns;
        a_tb <= "1110"; Control_tb <= "000"; wait for 20 ns;
        a_tb <= "1110"; Control_tb <= "001"; wait for 20 ns;
        a_tb <= "1110"; Control_tb <= "010"; wait for 20 ns;
        a_tb <= "1110"; Control_tb <= "011"; wait for 20 ns;
        a_tb <= "1110"; Control_tb <= "100"; wait for 20 ns;
        a_tb <= "1110"; Control_tb <= "101"; wait for 20 ns;
        a_tb <= "1110"; Control_tb <= "110"; wait for 20 ns;
        a_tb <= "1110"; Control_tb <= "111"; wait for 20 ns;
        a_tb <= "1111"; Control_tb <= "000"; wait for 20 ns;
        a_tb <= "1111"; Control_tb <= "001"; wait for 20 ns;
        a_tb <= "1111"; Control_tb <= "010"; wait for 20 ns;
        a_tb <= "1111"; Control_tb <= "011"; wait for 20 ns;
        a_tb <= "1111"; Control_tb <= "100"; wait for 20 ns;
        a_tb <= "1111"; Control_tb <= "101"; wait for 20 ns;
        a_tb <= "1111"; Control_tb <= "110"; wait for 20 ns;
        a_tb <= "1111"; Control_tb <= "111"; wait for 20 ns;
end process apply_test_cases;
end Beh_tb;
