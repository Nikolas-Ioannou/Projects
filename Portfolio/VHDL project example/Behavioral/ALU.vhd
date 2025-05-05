----------------------------------------------------------------------------------
-- Company: 
-- Engineer: 
-- 
-- Create Date: 
-- Design Name: 
-- Module Name: 
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
use IEEE.NUMERIC_STD.ALL;

-- Uncomment the following library declaration if instantiating
-- any Xilinx leaf cells in this code.
--library UNISIM;
--use UNISIM.VComponents.all;

entity ALU is
     Port ( a : in STD_LOGIC_VECTOR (3 downto 0);
           Control : in STD_LOGIC_VECTOR (2 downto 0);
           Result : out STD_LOGIC_VECTOR (3 downto 0));
end ALU;


architecture Behavioral of ALU is 
begin
process(a, Control)
begin
    if Control(2) = '0' then  -- Doing Shift
        if Control(1) = '0' then  -- sll or sla
            if a(3) = '1' then -- Overflow for sll
                Result <= "1111";
            elsif Control(0) = '0' then -- sll
                Result <= a(2 downto 0) & '0';
            else -- sla
                if a(2) = '1' then -- Overflow
                    Result <= "1111";
                else
                    Result <= a(1 downto 0) & "00";
                end if;
            end if;
        else  -- srl or sra
            if Control(0) = '0' then -- srl
                Result <= '0' & a(3 downto 1);  
            else -- sra
                Result <= "00" & a(3 downto 2);  
            end if;
        end if;
    else  -- Rotate
        if Control(1) = '0' then  -- rol
            Result <= a(2 downto 0) & a(3);
        else  -- ror
            Result <= a(0) & a(3 downto 1);
        end if;
    end if;
end process;
end Behavioral;
